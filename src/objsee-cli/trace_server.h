//
//  trace_server.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#ifndef TRACE_SERVER_H
#define TRACE_SERVER_H

/**
 * Run the trace server on specified port
 *
 * @param config The configuration to use for the server
 * @param traced_pid The pid of the process to trace
 * @return 0 on success, 1 on error
 */
int run_trace_server(tracer_config_t *config, pid_t traced_pid);

#endif // TRACE_SERVER_H
