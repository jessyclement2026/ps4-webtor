#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define LISTEN_PORT 8080
#define ARENA_SIZE  (64 * 1024 * 1024)
static char *arena_base = NULL;
static size_t arena_offset = 0;
static char *last_origin = NULL;

typedef struct { size_t size; } alloc_hdr_t;

void *malloc(size_t size) {
    if (size == 0) size = 1;
    size = (size + 15) & ~((size_t)15);
    size_t total = size + sizeof(alloc_hdr_t);
    if (!arena_base) {
        arena_base = mmap(NULL, ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (arena_base == MAP_FAILED) { arena_base = NULL; return NULL; }
    }
    size_t off = __atomic_fetch_add(&arena_offset, total, __ATOMIC_SEQ_CST);
    if (off + total > ARENA_SIZE) return NULL;
    alloc_hdr_t *hdr = (alloc_hdr_t *)(arena_base + off);
    hdr->size = size;
    return (void *)(hdr + 1);
}

void free(void *ptr) { (void)ptr; }

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) return NULL;
    alloc_hdr_t *hdr = ((alloc_hdr_t *)ptr) - 1;
    if (hdr->size >= size) return ptr;
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, hdr->size);
    return new_ptr;
}
int sceKernelSendNotificationRequest(int device, void *req, size_t size, int blocking);

static void ps4_notify(const char *msg) {
    char *req = calloc(1, 1080);
    strncpy(req + 45, msg, 1023);
    sceKernelSendNotificationRequest(0, req, 1080, 0);
}

static char *my_strcasestr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, nlen) == 0) return (char *)haystack;
    }
    return NULL;
}

static void url_decode(const char *src, char *dst, size_t dst_sz) {
    size_t di = 0;
    for (size_t si = 0; *(src + si) && di + 1 < dst_sz; si++) {
        if (*(src + si) == '%' && isxdigit((unsigned char)*(src + si + 1)) && isxdigit((unsigned char)*(src + si + 2))) {
            char *hex = calloc(1, 4);
            *(hex + 0) = *(src + si + 1);
            *(hex + 1) = *(src + si + 2);
            *(dst + di++) = (char)strtol(hex, NULL, 16);
            si += 2;
        } else if (*(src + si) == '+') {
            *(dst + di++) = ' ';
        } else {
            *(dst + di++) = *(src + si);
        }
    }
    *(dst + di) = '\0';
}

static void url_encode(const char *src, char *dst, size_t dst_sz) {
    const char *hex = "0123456789ABCDEF";
    size_t di = 0;
    for (size_t si = 0; *(src + si) && di + 4 < dst_sz; si++) {
        unsigned char c = (unsigned char)*(src + si);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *(dst + di++) = (char)c;
        } else {
            *(dst + di++) = '%';
            *(dst + di++) = *(hex + ((c >> 4) & 0xF));
            *(dst + di++) = *(hex + (c & 0xF));
        }
    }
    *(dst + di) = '\0';
}
static int socks5_connect(const char *host, uint16_t port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9050);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(s); return -1; }

    unsigned char *greet = malloc(4);
    *(greet + 0) = 0x05; *(greet + 1) = 0x01; *(greet + 2) = 0x00;
    if (send(s, greet, 3, 0) != 3) { close(s); return -1; }

    unsigned char *resp = malloc(4);
    if (recv(s, resp, 2, MSG_WAITALL) != 2 || *(resp + 1) != 0x00) { close(s); return -1; }

    size_t hlen = strlen(host);
    if (hlen > 255) { close(s); return -1; }
    unsigned char *req = malloc(300);
    *(req + 0) = 0x05; *(req + 1) = 0x01; *(req + 2) = 0x00; *(req + 3) = 0x03;
    *(req + 4) = (unsigned char)hlen;
    memcpy(req + 5, host, hlen);
    *(req + 5 + hlen) = (port >> 8) & 0xFF;
    *(req + 6 + hlen) = port & 0xFF;

    if (send(s, req, 7 + hlen, 0) != (ssize_t)(7 + hlen)) { close(s); return -1; }
    unsigned char *reply = malloc(8);
    if (recv(s, reply, 4, MSG_WAITALL) != 4 || *(reply + 1) != 0x00) { close(s); return -1; }

    char *tmp = malloc(300);
    if (*(reply + 3) == 0x01) { recv(s, tmp, 6, MSG_WAITALL); }
    else if (*(reply + 3) == 0x04) { recv(s, tmp, 18, MSG_WAITALL); }
    else if (*(reply + 3) == 0x03) {
        unsigned char alen = 0;
        recv(s, &alen, 1, MSG_WAITALL);
        recv(s, tmp, alen + 2, MSG_WAITALL);
    }
    return s;
}

static void resolve_rel_url(const char *base, const char *href, char *out, size_t out_sz) {
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
        strncpy(out, href, out_sz - 1);
        *(out + out_sz - 1) = '\0';
        return;
    }
    if (strncmp(href, "//", 2) == 0) {
        snprintf(out, out_sz, "http:%s", href);
        return;
    }
    strncpy(out, base, out_sz - 1);
    *(out + out_sz - 1) = '\0';
    char *p = strstr(out, "://");
    if (!p) return;
    if (*href == '/') {
        char *slash = strchr(p + 3, '/');
        if (slash) *slash = '\0';
        strncat(out, href, out_sz - strlen(out) - 1);
    } else if (*href == '?') {
        char *q = strchr(p + 3, '?');
        if (q) *q = '\0';
        strncat(out, href, out_sz - strlen(out) - 1);
    } else {
        char *last = strrchr(p + 3, '/');
        if (last) *(last + 1) = '\0';
        else strncat(out, "/", out_sz - strlen(out) - 1);
        strncat(out, href, out_sz - strlen(out) - 1);
    }
}
static void serve_shell(int cfd) {
    const char *hdr =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n\r\n";
    send(cfd, hdr, strlen(hdr), 0);

    const char *html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'><title>PS4 WebTor</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0;font-family:sans-serif}"
        "body{background:#12101a;color:#eee;display:flex;flex-direction:column;height:100vh;overflow:hidden}"
        "#bar{background:#1e1a2e;padding:10px 14px;display:flex;gap:8px;align-items:center;border-bottom:2px solid #6b3fa0}"
        "#url-wrap{flex:1;display:flex;align-items:center;background:#0d0b14;border:1px solid #4c3575;border-radius:6px;padding-right:6px}"
        "#url{flex:1;padding:10px 12px;border:none;background:transparent;color:#fff;font-size:16px;outline:none}"
        "#clr{background:transparent;color:#8c82a8;padding:4px 8px;font-size:16px}"
        "#clr:hover{color:#fff;background:transparent}"
        "button,.bm{background:#6b3fa0;color:#fff;border:none;padding:10px 14px;border-radius:6px;font-size:15px;cursor:pointer;text-decoration:none;display:inline-flex;align-items:center;gap:4px}"
        "button:hover,.bm:hover{background:#8452c2}"
        ".sec{background:#2d2647}"
        "#bms{background:#171424;padding:6px 14px;display:flex;gap:8px;flex-wrap:wrap;align-items:center;border-bottom:1px solid #2b2440}"
        ".bm{padding:5px 10px;font-size:13px;background:#2a2342}"
        ".del{color:#ff7b7b;margin-left:6px;font-weight:bold;cursor:pointer}"
        "#status{margin-left:auto;font-size:12px;color:#9a90b8}"
        "iframe{flex:1;width:100%;border:none;background:#12101a}"
        "</style></head><body>"
        "<form id='bar' onsubmit='go(document.getElementById(\"url\").value);return false;'>"
        "<button type='button' class='sec' onclick='history.back()' title='Back'>&larr;</button>"
        "<button type='button' class='sec' onclick='reloadFrame()' title='Reload'><svg width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='3' stroke-linecap='round' stroke-linejoin='round'><path d='M21 12a9 9 0 1 1-3-6.7L21 8'/><polyline points='21 3 21 8 16 8'/></svg></button>"
        "<div id='url-wrap'>"
        "<input id='url' type='text' placeholder='Enter URL...' autocomplete='off'>"
        "<button type='button' id='clr' onclick='var i=document.getElementById(\"url\");i.value=\"\";i.focus();' title='Clear'>&times;</button>"
        "</div>"
        "<button type='submit' id='gobtn'>Go</button>"
        "<button type='button' class='sec' onclick='addBm()' title='Save Bookmark'>+ Save</button>"
        "<button type='button' class='sec' onclick='zoom(-0.1)' title='Zoom Out'>A-</button>"
        "<button type='button' class='sec' onclick='zoom(0.1)' title='Zoom In'>A+</button>"
        "</form>"
        "<div id='bms'>"
        "<a class='bm' href='#' onclick='go(\"https://github.com\");return false;'>GitHub: https://github.com/jessyclement2026/</a>"
        "<span id='user-bms' style='display:inline-flex;gap:8px;flex-wrap:wrap'></span>"
        "<span id='status'>Ready</span>"
        "</div>"
        "<iframe id='view' name='view' onload='doneLoad()' srcdoc=\"<body style='background:#12101a;color:#8c82a8;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:90vh;margin:0'><p>Enter a URL above to browse via Tor.</p></body>\"></iframe>"
        "<script>"
        "var z=1,busy=false;"
        "function setSt(t,b){busy=b;document.getElementById('status').textContent=t;document.getElementById('gobtn').textContent=b?'Stop':'Go';}"
        "function doneLoad(){if(busy)setSt('Ready',false);}"
        "function go(u){if(busy){var f=document.getElementById('view');f.src='about:blank';setSt('Stopped',false);return;}"
        "u=u.trim();if(!u)return;if(!/^https?:\\/\\//i.test(u))u='http://'+u;"
        "document.getElementById('url').value=u;"
        "var f=document.getElementById('view');f.removeAttribute('srcdoc');f.style.background='#fff';"
        "setSt('Loading via Tor...',true);f.src='/fetch?url='+encodeURIComponent(u);}"
        "function reloadFrame(){var f=document.getElementById('view');if(f.src&&f.src!=='about:blank'){setSt('Loading via Tor...',true);f.src=f.src;}}"
        "function zoom(d){z=Math.max(0.6,Math.min(1.8,z+d));document.getElementById('view').style.zoom=z;}"
        "function renderBms(){var b=JSON.parse(localStorage.getItem('wt_bms')||'[]'),h='';"
        "for(var i=0;i<b.length;i++){h+=\"<span class='bm'><a href='#' style='color:#fff;text-decoration:none' onclick='go(\\\"\"+b[i].u+\"\\\");return false;'>\"+b[i].n+\"</a><span class='del' onclick='delBm(\"+i+\")'>&times;</span></span>\";}"
        "document.getElementById('user-bms').innerHTML=h;}"
        "function addBm(){var u=document.getElementById('url').value.trim();if(!u)return;var n=u.replace(/^https?:\\/\\//,'').split('/').slice(0,24);"
        "var b=JSON.parse(localStorage.getItem('wt_bms')||'[]');b.push({n:n,u:u});localStorage.setItem('wt_bms',JSON.stringify(b));renderBms();}"
        "function delBm(i){var b=JSON.parse(localStorage.getItem('wt_bms')||'[]');b.splice(i,1);localStorage.setItem('wt_bms',JSON.stringify(b));renderBms();}"
        "renderBms();"
        "window.addEventListener('message',function(e){if(e.data&&e.data.torUrl){document.getElementById('url').value=e.data.torUrl;setSt('Ready',false);}});"
        "</script></body></html>";
    send(cfd, html, strlen(html), 0);

}
static void handle_fetch(int cfd, const char *target_url) {
    char *cur_url = malloc(2048);
    char *host = malloc(512);
    char *path = malloc(1536);
    char *req = malloc(4096);
    char *hdr = malloc(8192);
    char *next_url = malloc(2048);
    char *ctype = malloc(256);
    char *out_hdr = malloc(512);
    char *raw = malloc(1024);
    char *abs_u = malloc(2048);
    char *enc_u = malloc(4096);
    char *proxied = malloc(4200);
    char *hook = malloc(4096);

    strncpy(cur_url, target_url, 2047);
    *(cur_url + 2047) = '\0';

    for (int redir = 0; redir < 5; redir++) {
        int use_tls = (strncmp(cur_url, "https://", 8) == 0);
        uint16_t port = use_tls ? 443 : 80;
        const char *host_start = cur_url + (use_tls ? 8 : 7);
        const char *slash = strchr(host_start, '/');

        if (slash) {
            size_t hlen = slash - host_start;
            if (hlen >= 511) hlen = 511;
            memcpy(host, host_start, hlen);
            *(host + hlen) = '\0';
            strncpy(path, slash, 1535);
            *(path + 1535) = '\0';
        } else {
            strncpy(host, host_start, 511);
            *(host + 511) = '\0';
            strcpy(path, "/");
        }

        if (!last_origin) last_origin = malloc(512);
        snprintf(last_origin, 512, "%s://%s", use_tls ? "https" : "http", host);

        char *colon = strchr(host, ':');
        if (colon) {
            port = (uint16_t)atoi(colon + 1);
            *colon = '\0';
        }

        int tfd = socks5_connect(host, port);
        if (tfd < 0) {
            const char *err = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
                              "<body style='background:#12101a;color:#ff7b7b;font-family:sans-serif;padding:30px'>"
                              "<h2>Tor Connection Failed</h2><p>Could not reach hidden service via 127.0.0.1:9050. Make sure ps4-tor.elf is running.</p></body>";
            send(cfd, err, strlen(err), 0);
            return;
        }

        SSL_CTX *ctx = NULL;
        SSL *ssl = NULL;
        if (use_tls) {
            ctx = SSL_CTX_new(TLS_client_method());
            SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
            ssl = SSL_new(ctx);
            SSL_set_fd(ssl, tfd);
            SSL_set_tlsext_host_name(ssl, host);
            if (SSL_connect(ssl) <= 0) {
                SSL_free(ssl); SSL_CTX_free(ctx); close(tfd);
                if (strncmp(cur_url, "https://", 8) == 0) {
                    memmove(cur_url + 4, cur_url + 5, strlen(cur_url + 5) + 1);
                    memcpy(cur_url, "http://", 7);
                    continue;
                }
                const char *err = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
                                  "<body style='background:#12101a;color:#ff7b7b;font-family:sans-serif;padding:30px'>"
                                  "<h2>TLS Handshake Failed</h2><p>Could not establish TLS connection with server.</p></body>";
                send(cfd, err, strlen(err), 0);
                return;
            }
        }

        int rlen = snprintf(req, 4096,
            "GET %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; rv:128.0) Gecko/20100101 Firefox/128.0\r\n"
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/*,*/*;q=0.8\r\n"
            "Connection: close\r\n\r\n",
            path, host);

        if (use_tls) SSL_write(ssl, req, rlen);
        else send(tfd, req, rlen, 0);

        int hlen = 0;
        while (hlen < 8190) {
            int n = use_tls ? SSL_read(ssl, hdr + hlen, 1) : recv(tfd, hdr + hlen, 1, 0);
            if (n <= 0) break;
            hlen += n;
            *(hdr + hlen) = '\0';
            if (hlen >= 4 && strcmp(hdr + hlen - 4, "\r\n\r\n") == 0) break;
        }

        if (strncmp(hdr, "HTTP/1.1 30", 11) == 0 || strncmp(hdr, "HTTP/1.0 30", 11) == 0) {
            char *loc = my_strcasestr(hdr, "\r\nLocation:");
            if (loc) {
                loc += 11;
                while (*loc == ' ') loc++;
                char *eol = strstr(loc, "\r\n");
                if (eol) *eol = '\0';
                resolve_rel_url(cur_url, loc, next_url, 2048);
                strncpy(cur_url, next_url, 2047);
                if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); }
                close(tfd);
                continue;
            }
        }
        strcpy(ctype, "text/html; charset=utf-8");
        char *ct = my_strcasestr(hdr, "\r\nContent-Type:");
        if (ct) {
            ct += 15;
            while (*ct == ' ') ct++;
            char *eol = strstr(ct, "\r\n");
            size_t clen = eol ? (size_t)(eol - ct) : strlen(ct);
            if (clen >= 255) clen = 255;
            memcpy(ctype, ct, clen);
            *(ctype + clen) = '\0';
        }

        size_t cap = 65536, bsize = 0;
        char *body = malloc(cap);
        while (body && bsize < 4 * 1024 * 1024) {
            if (bsize + 4096 >= cap) {
                cap *= 2;
                char *nb = realloc(body, cap);
                if (!nb) break;
                body = nb;
            }
            int n = use_tls ? SSL_read(ssl, body + bsize, 4096) : recv(tfd, body + bsize, 4096, 0);
            if (n <= 0) break;
            bsize += n;
        }
        if (body) *(body + bsize) = '\0';

        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); }
        close(tfd);

        int ohlen = snprintf(out_hdr, 512,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Connection: close\r\n\r\n", ctype);
        send(cfd, out_hdr, ohlen, 0);

        if (body && my_strcasestr(ctype, "text/html") != NULL) {
            const char *p = body;
            while (*p) {
                const char *h1 = my_strcasestr(p, "href=");
                const char *h2 = my_strcasestr(p, "src=");
                const char *hit = NULL;
                int attr_len = 0;
                if (h1 && (!h2 || h1 < h2)) { hit = h1; attr_len = 5; }
                else if (h2) { hit = h2; attr_len = 4; }

                if (!hit) {
                    send(cfd, p, strlen(p), 0);
                    break;
                }

                send(cfd, p, hit - p + attr_len, 0);
                p = hit + attr_len;

                char q = *p;
                if (q != '"' && q != '\'') continue;
                send(cfd, &q, 1, 0);
                p++;

                const char *end_q = strchr(p, q);
                if (!end_q) { send(cfd, p, strlen(p), 0); break; }

                size_t ulen = end_q - p;
                if (ulen > 0 && ulen < 900 && strncmp(p, "data:", 5) != 0 && strncmp(p, "javascript:", 11) != 0 && *p != '#') {
                    memcpy(raw, p, ulen);
                    *(raw + ulen) = '\0';
                    resolve_rel_url(cur_url, raw, abs_u, 2048);
                    url_encode(abs_u, enc_u, 4096);
                    int plen = snprintf(proxied, 4200, "/fetch?url=%s", enc_u);
                    send(cfd, proxied, plen, 0);
                } else {
                    send(cfd, p, ulen, 0);
                }
                send(cfd, &q, 1, 0);
                p = end_q + 1;
            }

            int hlen_js = snprintf(hook, 4096,
                "<script>"
                "if(window.parent)window.parent.postMessage({torUrl:'%s'},'*');"
                "</script>", cur_url);
            send(cfd, hook, hlen_js, 0);
        } else if (body && bsize > 0) {
            send(cfd, body, bsize, 0);
        }
        return;
    }
}

static void handle_client(int cfd) {
    char *req = malloc(4096);
    char *method = malloc(32);
    char *path = malloc(2048);
    char *decoded = malloc(2048);
    char *rel_target = malloc(2560);

    ssize_t n = recv(cfd, req, 4095, 0);
    if (n <= 0) { close(cfd); return; }
    *(req + n) = '\0';

    if (sscanf(req, "%31s %2047s", method, path) != 2) {
        close(cfd);
        return;
    }

    if (strncmp(path, "/fetch?url=", 11) == 0) {
        url_decode(path + 11, decoded, 2048);
        handle_fetch(cfd, decoded);
    } else if (strcmp(path, "/quit") == 0) {
        const char *bye = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nWebGate stopped.";
        send(cfd, bye, strlen(bye), 0);
        shutdown(cfd, SHUT_WR);
        close(cfd);
        ps4_notify("Tor WebGate stopped.");
        exit(0);
    } else if (strcmp(path, "/") == 0) {
        serve_shell(cfd);
    } else if (last_origin != NULL && *path == '/') {
        snprintf(rel_target, 2560, "%s%s", last_origin, path);
        handle_fetch(cfd, rel_target);
    } else {
        serve_shell(cfd);
    }
    shutdown(cfd, SHUT_WR);
    close(cfd);
}

int main(void) {
    OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG, NULL);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { ps4_notify("WebGate error: socket() failed"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ps4_notify("WebGate already running on port 8080");
        close(srv);
        return 0;
    }

    if (listen(srv, 16) < 0) {
        ps4_notify("WebGate error: listen() failed");
        close(srv);
        return 1;
    }

    ps4_notify("Tor WebGate READY on port 8080!");

    while (1) {
        int cfd = accept(srv, NULL, NULL);
        if (cfd >= 0) handle_client(cfd);
    }

    close(srv);
    return 0;
}
