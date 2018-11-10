
#ifndef _PROXY_TARGET_HANDLER_H
#define _PROXY_TARGET_HANDLER_H

/**
 * Callback for input data from proxying target.
 */
void target_input_handler(int socket, void* arg);

/**
 * Callback for target socket hup.
 */
void target_hup_handler(int socket, void* arg);

#endif
