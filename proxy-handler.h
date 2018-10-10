
#include <cache.h>

typename struct proxy_data_struct {

} proxy_data_t;

bool proxy_data_init(proxy_data_t* data /*args*/);

bool proxy_queue(proxy_data_t* data);

void proxy_data_free(proxy_data_t* data);
