# #
# //  deploy_to_device.py
# //  libobjsee
# //
# //  Created by Ethan Arbuckle on 11/30/24.
# //

import os
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

DEVICE_SSH_PORT = "2222"
DEVICE_SSH_IP = "localhost"


def determine_jb_root_prefix() -> Path:
    try:
        output = subprocess.check_output(
            ["ssh", "-oStricthostkeychecking=no", "-oUserknownhostsfile=/dev/null", "-p", DEVICE_SSH_PORT, f"root@{DEVICE_SSH_IP}", "env"],
            text=True,
        )
        
        if "/var/jb/" in output:
            return Path("/var/jb/")
        elif "jbroot" in output:
            print(output)
            return Path("/var/containers/Bundle/Application/.jbroot-")
        else:
            return Path("/")
    except subprocess.CalledProcessError as exc:
        print(f"Failed to determine JB_ROOT_PREFIX with error: {exc}")
        return Path("/")
    
JB_ROOT_PREFIX = determine_jb_root_prefix()

def find_host_ldid2_path() -> Optional[Path]:
    assumed_path = Path("/opt/homebrew/bin/ldid2")
    if assumed_path.exists():
        return assumed_path

    try:
        output = subprocess.check_output(["which", "ldid2"], text=True)
        which_ret_path = Path(output.strip())
        if which_ret_path.exists():
            return which_ret_path
    except subprocess.CalledProcessError as exc:
        print(f"which[ldid2] failed with error: {exc}")
    return None


@dataclass
class BinaryInstallInformation:
    # The on-device path to copy the binary to
    on_device_path: Path
    # An entitlements file to sign the local binary with before copying to the device.
    # If no file is specified, the binary will be signed without explicit entitlements
    entitlements_file: Optional[Path] = None


BINARY_DEPLOY_INFO = {
    "cli": BinaryInstallInformation(
        JB_ROOT_PREFIX / "usr/bin/objsee", Path("src/objsee-cli/objsee-entitlements.xml").resolve()
    ),
    "libobjsee": BinaryInstallInformation(
        JB_ROOT_PREFIX / "usr/lib/libobjsee.dylib",)
}


def run_command_on_device(command: str) -> bytes:
    return subprocess.check_output(
        f'ssh -oStricthostkeychecking=no -oUserknownhostsfile=/dev/null -p {DEVICE_SSH_PORT} root@{DEVICE_SSH_IP} "{command}"',
        shell=True,
    )


def copy_file_to_device(local: Path, remote: Path) -> None:
    subprocess.check_output(
        f'scp -oStricthostkeychecking=no -oUserknownhostsfile=/dev/null -P {DEVICE_SSH_PORT} "{local.as_posix()}" root@{DEVICE_SSH_IP}:"{remote.as_posix()}"',
        shell=True,
    )


def local_sign_binary(local_path: Path, entitlements_file: Optional[Path]) -> None:
    local_ldid2_path = find_host_ldid2_path()
    if not local_ldid2_path or not local_ldid2_path.exists():
        raise Exception("Could not find ldid2 on host system")

    ldid_cmd_args = [local_ldid2_path.as_posix()]
    if entitlements_file:
        ldid_cmd_args += [f"-S{entitlements_file.as_posix()}"]
    else:
        ldid_cmd_args += ["-S"]
    ldid_cmd_args.append(local_path.as_posix())
    subprocess.check_output(ldid_cmd_args)


def deploy_to_device(local_path: Path, binary_deploy_info: BinaryInstallInformation) -> None:

    if not local_path.exists():
        raise Exception(f"local binary {local_path} does not exist")

    # Sign the binary locally
    if binary_deploy_info.entitlements_file:
        local_sign_binary(local_path, binary_deploy_info.entitlements_file)

    # Delete existing binary on-device if it exists
    try:
        run_command_on_device(f"rm {binary_deploy_info.on_device_path.as_posix()} || true")
    except subprocess.CalledProcessError as e:
        pass

    # Copy local signed binary to device
    try:
        copy_file_to_device(local_path, binary_deploy_info.on_device_path)
    except Exception as e:
        raise Exception(f"Failed to copy {binary_deploy_info.on_device_path.as_posix()} to device with error: {e}")

    on_device_ents_path = JB_ROOT_PREFIX / "tmp/entitlements.xml"
    try:
        if binary_deploy_info.entitlements_file and binary_deploy_info.entitlements_file.exists():
            copy_file_to_device(binary_deploy_info.entitlements_file, on_device_ents_path)
    except Exception as e:
        print(f"Failed to copy entitlements file to device with error: {e}")

    on_device_ldid_path = JB_ROOT_PREFIX / "usr/bin/ldid"
    run_command_on_device(
        f"{on_device_ldid_path.as_posix()} -S{on_device_ents_path.as_posix()} {binary_deploy_info.on_device_path.as_posix()}"
    )


if __name__ == "__main__":
    print("deploying binaries device")

    if "BUILT_PRODUCTS_DIR" not in os.environ:
        raise Exception("BUILT_PRODUCTS_DIR not found")

    BUILT_PRODUCTS_DIR = Path(os.environ["BUILT_PRODUCTS_DIR"])
    if not BUILT_PRODUCTS_DIR.exists():
        raise Exception("BUILT_PRODUCTS_DIR var exists but directory does not")

    for framework_path in BUILT_PRODUCTS_DIR.glob("*.framework"):
        fw_binary_path = framework_path / framework_path.stem
        if not fw_binary_path.exists():
            raise Exception(f"file does not exist: {fw_binary_path}")

        if framework_path.stem not in BINARY_DEPLOY_INFO:
            continue

        binary_deploy_info = BINARY_DEPLOY_INFO[framework_path.stem]
        deploy_to_device(fw_binary_path, binary_deploy_info)
    print("Done deploying binaries to device")
