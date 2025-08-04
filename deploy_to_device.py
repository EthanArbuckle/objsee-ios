# #
# //  deploy_to_device.py
# //  libobjsee
# //
# //  Created by Ethan Arbuckle on 11/30/24.
# //

import logging
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path

DEVICE_SSH_PORT = "2222"
DEVICE_SSH_IP = "localhost"


class CodeSignError(Exception):
    pass


class DeploymentError(Exception):
    pass


class BinaryNotFoundError(Exception):
    pass


@dataclass
class BinaryInstallInformation:
    on_device_path: Path
    entitlements_file: Path | None = None

    def __post_init__(self) -> None:
        root_prefix = determine_jb_root_prefix()
        if self.on_device_path:
            self.on_device_path = root_prefix / self.on_device_path


logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


def determine_jb_root_prefix() -> Path:
    try:
        command = [
            "ssh",
            "-oStricthostkeychecking=no",
            "-oUserknownhostsfile=/dev/null",
            "-p",
            DEVICE_SSH_PORT,
            f"root@{DEVICE_SSH_IP}",
            "env",
        ]
        output = subprocess.check_output(command, text=True)

        if "/var/jb/" in output:
            return Path("/var/jb/")
        elif "jbroot" in output:
            return Path("/var/containers/Bundle/Application/.jbroot-")
        else:
            return Path("/")
    except subprocess.CalledProcessError as exc:
        logger.warning(f"Failed to determine JB_ROOT_PREFIX with error: {exc}")
        return Path("/")


def find_host_ldid2_path() -> Path | None:
    try:
        environment = os.environ.copy()
        environment["PATH"] = f"{environment['PATH']}:/opt/homebrew/bin/"
        ldid_path_output = subprocess.check_output(["which", "ldid2"], text=True, env=environment)
        ldid_path = Path(ldid_path_output.strip())
        if ldid_path.exists():
            return ldid_path
    except subprocess.CalledProcessError as exc:
        logger.warning(f"Failed to find ldid2 on host with error: {exc}")
    return Path("ldid2")


BINARY_DEPLOY_INFO = {
    "cli": BinaryInstallInformation(Path("usr/bin/objsee"), Path("src/objsee-cli/objsee-entitlements.xml").resolve()),
    "libobjsee": BinaryInstallInformation(Path("Library/Frameworks/libobjsee.framework/libobjsee")),
}


def run_command_on_device(command: str) -> bytes:
    ssh_args = [
        "ssh",
        "-oStricthostkeychecking=no",
        "-oUserknownhostsfile=/dev/null",
        "-p",
        DEVICE_SSH_PORT,
        f"root@{DEVICE_SSH_IP}",
        command,
    ]
    return subprocess.check_output(ssh_args)


def copy_file_to_device(local: Path, remote: Path) -> None:
    scp_args = [
        "scp",
        "-oStricthostkeychecking=no",
        "-oUserknownhostsfile=/dev/null",
        "-P",
        DEVICE_SSH_PORT,
        local.as_posix(),
        f"root@{DEVICE_SSH_IP}:{remote.as_posix()}",
    ]
    subprocess.check_output(scp_args)


def local_sign_binary(binary_path: Path, entitlements_file: Path | None = None) -> None:
    local_ldid2_path = find_host_ldid2_path()
    if not local_ldid2_path:
        raise CodeSignError("Could not find ldid2 on host system")

    ldid_cmd_args = [local_ldid2_path.as_posix()]
    if entitlements_file:
        ldid_cmd_args += [f"-S{entitlements_file.as_posix()}"]
    else:
        ldid_cmd_args += ["-S"]
    ldid_cmd_args.append(binary_path.as_posix())

    try:
        subprocess.check_output(ldid_cmd_args)
    except subprocess.CalledProcessError as e:
        raise CodeSignError(f"Failed to sign binary with ldid2: {e}")


def deploy_to_device(local_path: Path, binary_deploy_info: BinaryInstallInformation) -> None:
    if not local_path.exists():
        raise BinaryNotFoundError(f"Binary not found at {local_path.as_posix()}")

    if binary_deploy_info.entitlements_file:
        local_sign_binary(local_path, binary_deploy_info.entitlements_file)

    # Delete existing binary on-device if it exists
    try:
        run_command_on_device(f"rm {binary_deploy_info.on_device_path.as_posix()} || true")
    except subprocess.CalledProcessError as e:
        logger.warning(f"Failed to delete existing binary on device with error: {e}")

    # Copy local signed binary to device
    try:
        copy_file_to_device(local_path, binary_deploy_info.on_device_path)
    except Exception as e:
        raise DeploymentError(
            f"Failed to copy {binary_deploy_info.on_device_path.as_posix()} to device with error: {e}"
        )

    root_prefix = determine_jb_root_prefix()
    on_device_ents_path = root_prefix / "tmp/entitlements.xml"
    try:
        if binary_deploy_info.entitlements_file and binary_deploy_info.entitlements_file.exists():
            copy_file_to_device(binary_deploy_info.entitlements_file, on_device_ents_path)
    except Exception as e:
        raise DeploymentError(f"Failed to copy entitlements file to device with error: {e}")

    on_device_ldid_path = root_prefix / "usr/bin/ldid"
    run_command_on_device(
        f"{on_device_ldid_path.as_posix()} -S{on_device_ents_path.as_posix()} {binary_deploy_info.on_device_path.as_posix()}"
    )


if __name__ == "__main__":
    logger.info("deploying binaries device")

    if "BUILT_PRODUCTS_DIR" not in os.environ:
        raise BinaryNotFoundError("BUILT_PRODUCTS_DIR var not found in environment")

    BUILT_PRODUCTS_DIR = Path(os.environ["BUILT_PRODUCTS_DIR"])
    if not BUILT_PRODUCTS_DIR.exists():
        raise BinaryNotFoundError(f"BUILT_PRODUCTS_DIR not found at {BUILT_PRODUCTS_DIR.as_posix()}")

    for framework_path in BUILT_PRODUCTS_DIR.glob("*.framework"):
        fw_binary_path = framework_path / framework_path.stem
        if not fw_binary_path.exists():
            raise BinaryNotFoundError(f"Binary not found at {fw_binary_path.as_posix()}")

        if framework_path.stem not in BINARY_DEPLOY_INFO:
            continue

        binary_deploy_info = BINARY_DEPLOY_INFO[framework_path.stem]
        deploy_to_device(fw_binary_path, binary_deploy_info)
    logger.info("Done deploying binaries to device")
