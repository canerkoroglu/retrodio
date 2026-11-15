#include "rg_err.h"

const char *rg_err_name(esp_err_t err)
{
    switch (err) {
    case RG_ERR_ABORTED:
        return "RG_ERR_ABORTED";
    case RG_ERR_CARD_FULL:
        return "RG_ERR_CARD_FULL";
    case RG_ERR_BAD_FORMAT:
        return "RG_ERR_BAD_FORMAT";
    case RG_ERR_VERIFY:
        return "RG_ERR_VERIFY";
    case RG_ERR_TIMEOUT:
        return "RG_ERR_TIMEOUT";
    default:
        return esp_err_to_name(err);
    }
}
