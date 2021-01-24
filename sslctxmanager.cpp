#include "sslctxmanager.h"

SslCtxManager::SslCtxManager() :
    ssl_ctx(SSL_CTX_new(TLS_server_method()))
{

}

SslCtxManager::~SslCtxManager()
{
    if (ssl_ctx)
        SSL_CTX_free(ssl_ctx);
}

SSL_CTX *SslCtxManager::get() const
{
    return ssl_ctx;
}
