//
//  tui_trace_server.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/23/24.
//

#ifndef tui_trace_server_h
#define tui_trace_server_h

/**
 * Run the curses-based tui trace server
 * 
 * @param config The configuration to use for the server
 * @return 0 on success, 1 on error
 */
int run_tui_trace_server(tracer_config_t *config);

#endif /* tui_trace_server_h */
