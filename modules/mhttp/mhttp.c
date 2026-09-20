#include "mhttp.h"

void mhttp_hdl_init(mhttp_hdl* hdl)
{
    if (! hdl)
        return;
    
    // TODO
}

int mhttp_send_req(mhttp_hdl* hdl, mhttp_req req, int (*rsp_cb)(mhttp_rsp), uint32_t timeout)
{
    if (! hdl)
        return -1;

    // TODO

    return 0;
}
