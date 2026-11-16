#include "gb_web.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rg_audio.h"
#include "rg_board.h"
#include "rg_catalog.h"
#include "rg_err.h"
#include "rg_wav.h"

#define PLAYER_PRIO  6 /* above the web server (5): a download never starves the speaker */
#define PLAYER_STACK 6144
#define FILE_CHUNK   4096
#define LIST_CHUNK   1024
#define MAX_CLIENTS  4

static const char *TAG = "gb_web";

static httpd_handle_t s_httpd;
static QueueHandle_t s_play_q; /* uint32_t message index, length 1 (newest request wins) */
static volatile uint32_t s_playing;
static volatile uint32_t s_play_start_ms;
static volatile uint32_t s_play_len_ms;
static volatile bool s_stop;
static volatile bool s_exit;
static volatile uint32_t s_clients;
static volatile uint32_t s_last_req_ms; /* 32-bit: read atomically by the state machine task */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void touch(void)
{
    uint32_t t = now_ms();
    s_last_req_ms = t ? t : 1;
}

static void msg_path(uint32_t index, char *path, size_t len)
{
    char name[24];
    rg_catalog_format_name(index, name, sizeof(name));
    snprintf(path, len, "%s/%s", RG_DIR_RECORDINGS, name);
}

/* ---------------------------------------------------------------- player */

static bool play_abort(void *ctx)
{
    (void)ctx;
    return s_stop || s_exit || uxQueueMessagesWaiting(s_play_q) > 0;
}

static void player_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t index;
        if (xQueueReceive(s_play_q, &index, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_stop = false;
        char path[64];
        msg_path(index, path, sizeof(path));
        rg_wav_info_t info;
        if (rg_wav_probe(path, &info) != ESP_OK || !rg_wav_is_playable(&info)) {
            ESP_LOGW(TAG, "%s: not playable", path);
            continue;
        }
        s_play_len_ms = (uint32_t)((uint64_t)info.data_bytes * 1000 / RG_BYTES_PER_SECOND);
        s_play_start_ms = now_ms();
        s_playing = index;
        ESP_LOGI(TAG, "play %s (%lu ms)", path, (unsigned long)s_play_len_ms);

        esp_err_t err = rg_audio_open(RG_AUDIO_TX, false);
        if (err == ESP_OK) {
            err = rg_audio_write_silence(RG_SAMPLE_RATE_HZ / 20); /* amp leaves standby */
        }
        if (err == ESP_OK) {
            err = rg_audio_play_wav(path, play_abort, NULL);
        }
        if (err == ESP_OK) {
            err = rg_audio_drain();
        }
        rg_audio_close(); /* BCLK and LRC stop together: no DC step at the amp */
        if (err != ESP_OK && err != RG_ERR_ABORTED) {
            ESP_LOGW(TAG, "playback %s: %s", path, rg_err_name(err));
        }
        s_playing = 0;
    }
}

/* ---------------------------------------------------------------- web page */

static const char PAGE[] =
    "<!doctype html><html lang='tr'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'><title>Ses kayıtları</title><style>"
    "body{font-family:system-ui,-apple-system,sans-serif;margin:0;background:#f6f4ef;color:#222}"
    "header{position:sticky;top:0;background:#fff;border-bottom:1px solid #ddd;padding:12px 16px}"
    "h1{font-size:18px;margin:0 0 6px}"
    "#now{font-size:14px;color:#555;display:flex;align-items:center;gap:8px;min-height:34px;flex-wrap:wrap}"
    "ul{list-style:none;margin:0;padding:8px 16px 80px}"
    "li{display:flex;align-items:center;gap:8px;background:#fff;border:1px solid #e3e0d8;border-radius:10px;"
    "padding:10px 12px;margin:8px 0}"
    "li.on{border-color:#2a7;background:#eefaf3}"
    ".t{flex:1;font-size:15px}.t small{color:#777;margin-left:8px}"
    "button,a.b{font:inherit;font-size:14px;padding:8px 12px;border-radius:8px;border:1px solid #bbb;"
    "background:#fff;color:#222;text-decoration:none}"
    "button.p{background:#1f6f4a;border-color:#1f6f4a;color:#fff}"
    "footer{position:fixed;bottom:0;left:0;right:0;background:#fff;border-top:1px solid #ddd;padding:10px 16px;"
    "display:flex;justify-content:space-between;align-items:center;font-size:13px;color:#666}"
    "@media(prefers-color-scheme:dark){body{background:#1b1b1a;color:#eee}"
    "header,footer,li,button,a.b{background:#262624;color:#eee;border-color:#444}"
    "li.on{background:#173326;border-color:#2a7}.t small,#now,footer{color:#aaa}"
    "button.p{background:#1f6f4a;color:#fff}}"
    "</style></head><body>"
    "<header><h1>Ses kayıtları</h1><div id='now'>Yükleniyor…</div></header>"
    "<ul id='list'></ul>"
    "<footer><span id='cnt'></span><button onclick='bye()'>Kapat</button></footer>"
    "<script>"
    "var cur=0;"
    "function f(ms){var s=Math.round(ms/1000);return Math.floor(s/60)+':'+('0'+s%60).slice(-2)}"
    "function nm(n){return 'MSG_'+('00000'+n).slice(-6)+'.wav'}"
    "function post(u){return fetch(u,{method:'POST'})}"
    "function play(n){post('/api/play?n='+n).then(st)}"
    "function stop(){post('/api/stop').then(st)}"
    "function bye(){if(confirm('Dinleme modu kapansın mı? Cihaz normal moda döner.')){post('/api/exit');"
    "document.body.innerHTML='<p style=padding:16px>Dinleme modu kapandı. Wi-Fi birazdan kapanacak.</p>'}}"
    "function mark(){document.querySelectorAll('li.on').forEach(e=>e.classList.remove('on'));"
    "var e=document.getElementById('m'+cur);if(e)e.classList.add('on')}"
    "function load(){fetch('/api/list').then(r=>r.json()).then(a=>{var h='';a.forEach(x=>{"
    "h+='<li id=m'+x[0]+'><span class=t>Mesaj '+x[0]+'<small>'+f(x[1])+'</small></span>"
    "<button class=p onclick=play('+x[0]+')>Çal</button><a class=b href=/rec/'+nm(x[0])+' download>İndir</a></li>'});"
    "document.getElementById('list').innerHTML=h||'<li>Henüz kayıt yok.</li>';"
    "document.getElementById('cnt').textContent=a.length+' kayıt';mark()})}"
    "function st(){fetch('/api/status').then(r=>r.json()).then(s=>{cur=s.playing;"
    "var n=document.getElementById('now');n.innerHTML=cur?'Çalıyor: Mesaj '+cur+' ('+f(s.pos)+' / '+f(s.len)+')"
    " <button onclick=stop()>Durdur</button>':'Ahizeyi kulağınıza tutun ve bir kayıt seçin.';mark()}).catch(()=>{})}"
    "load();st();setInterval(st,1000);"
    "</script></body></html>";

/* ---------------------------------------------------------------- HTTP handlers */

static esp_err_t send_json(httpd_req_t *r, const char *json)
{
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, json);
}

static esp_err_t h_index(httpd_req_t *r)
{
    touch();
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, PAGE, sizeof(PAGE) - 1);
}

typedef struct {
    uint32_t index;
    uint32_t ms;
} item_t;

static int newest_first(const void *a, const void *b)
{
    uint32_t x = ((const item_t *)a)->index, y = ((const item_t *)b)->index;
    return x < y ? 1 : (x > y ? -1 : 0);
}

/* [[index, duration_ms], ...], newest first. */
static esp_err_t h_list(httpd_req_t *r)
{
    touch();
    size_t cap = 64, n = 0;
    item_t *items = malloc(cap * sizeof(*items));
    char *out = malloc(LIST_CHUNK);
    if (!items || !out) {
        free(items);
        free(out);
        return httpd_resp_send_500(r);
    }
    DIR *d = opendir(RG_DIR_RECORDINGS);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            uint32_t index;
            if (!rg_catalog_parse_name(e->d_name, &index)) {
                continue;
            }
            char path[64];
            msg_path(index, path, sizeof(path));
            struct stat st;
            if (stat(path, &st) != 0 || st.st_size <= RG_WAV_HEADER_SIZE) {
                continue;
            }
            if (n == cap) {
                item_t *grown = realloc(items, cap * 2 * sizeof(*items));
                if (!grown) {
                    break;
                }
                items = grown;
                cap *= 2;
            }
            items[n].index = index;
            items[n].ms = (uint32_t)((uint64_t)(st.st_size - RG_WAV_HEADER_SIZE) * 1000 / RG_BYTES_PER_SECOND);
            n++;
        }
        closedir(d);
    }
    qsort(items, n, sizeof(*items), newest_first);

    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t err = ESP_OK;
    size_t used = 0;
    out[used++] = '[';
    for (size_t i = 0; i < n && err == ESP_OK; i++) {
        if (used > LIST_CHUNK - 32) {
            err = httpd_resp_send_chunk(r, out, used);
            used = 0;
        }
        used += snprintf(out + used, LIST_CHUNK - used, "%s[%lu,%lu]", i ? "," : "", (unsigned long)items[i].index,
                         (unsigned long)items[i].ms);
    }
    if (err == ESP_OK) {
        out[used++] = ']';
        err = httpd_resp_send_chunk(r, out, used);
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(r, NULL, 0);
    }
    free(items);
    free(out);
    return err;
}

static bool query_index(httpd_req_t *r, uint32_t *index)
{
    char q[32], v[12];
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "n", v, sizeof(v)) != ESP_OK) {
        return false;
    }
    char *end;
    unsigned long x = strtoul(v, &end, 10);
    if (end == v || *end || x < 1 || x > 999999) {
        return false;
    }
    *index = (uint32_t)x;
    return true;
}

static esp_err_t h_play(httpd_req_t *r)
{
    touch();
    uint32_t index;
    if (!query_index(r, &index)) {
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "n");
    }
    char path[64];
    msg_path(index, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) != 0) {
        return httpd_resp_send_404(r);
    }
    xQueueOverwrite(s_play_q, &index); /* a running playback sees the request and stops */
    return send_json(r, "{\"ok\":true}");
}

static esp_err_t h_stop(httpd_req_t *r)
{
    touch();
    xQueueReset(s_play_q);
    s_stop = true;
    return send_json(r, "{\"ok\":true}");
}

static esp_err_t h_status(httpd_req_t *r)
{
    touch();
    uint32_t playing = s_playing, len = s_play_len_ms;
    uint32_t pos = playing ? now_ms() - s_play_start_ms : 0;
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"playing\":%lu,\"pos\":%lu,\"len\":%lu}", (unsigned long)playing,
             (unsigned long)(pos < len ? pos : len), (unsigned long)len);
    return send_json(r, buf);
}

static esp_err_t h_exit(httpd_req_t *r)
{
    touch();
    s_exit = true;
    return send_json(r, "{\"ok\":true}");
}

/* /rec/MSG_000012.wav: the file as stored on the card. */
static esp_err_t h_file(httpd_req_t *r)
{
    touch();
    char name[24];
    const char *p = r->uri + strlen("/rec/");
    size_t len = strcspn(p, "?");
    uint32_t index;
    if (len >= sizeof(name)) {
        return httpd_resp_send_404(r);
    }
    memcpy(name, p, len);
    name[len] = 0;
    if (!rg_catalog_parse_name(name, &index)) {
        return httpd_resp_send_404(r);
    }
    char path[64];
    msg_path(index, path, sizeof(path));
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return httpd_resp_send_404(r);
    }
    char *buf = malloc(FILE_CHUNK);
    if (!buf) {
        close(fd);
        return httpd_resp_send_500(r);
    }
    char disp[64];
    rg_catalog_format_name(index, name, sizeof(name));
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", name);
    httpd_resp_set_type(r, "audio/wav");
    httpd_resp_set_hdr(r, "Content-Disposition", disp);
    esp_err_t err = ESP_OK;
    ssize_t got;
    while (err == ESP_OK && (got = read(fd, buf, FILE_CHUNK)) > 0) {
        err = httpd_resp_send_chunk(r, buf, (size_t)got);
        touch(); /* a long download keeps the mode alive */
    }
    close(fd);
    free(buf);
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(r, NULL, 0);
    }
    return err;
}

/* ---------------------------------------------------------------- Wi-Fi */

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        s_clients++;
        touch();
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED && s_clients) {
        s_clients--;
    }
}

static esp_err_t start_ap(const rg_wifi_cfg_t *cfg)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_ap(), ESP_FAIL, TAG, "ap netif");
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage"); /* never writes NVS */
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL), TAG, "events");

    wifi_config_t wc = {0};
    size_t ssid_len = strlen(cfg->ssid);
    memcpy(wc.ap.ssid, cfg->ssid, ssid_len);
    wc.ap.ssid_len = (uint8_t)ssid_len;
    strlcpy((char *)wc.ap.password, cfg->password, sizeof(wc.ap.password));
    wc.ap.channel = CONFIG_GB_WIFI_CHANNEL;
    wc.ap.max_connection = MAX_CLIENTS;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK; /* the caller guarantees an 8..63 character password */
    wc.ap.pmf_cfg.required = false;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wc), TAG, "config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    /* Super Mini boards have a small chip antenna next to the regulator: full power often makes
     * the link worse, not better. */
    err = esp_wifi_set_max_tx_power(CONFIG_GB_WIFI_TX_POWER_QDBM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tx power: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

static esp_err_t start_http(void)
{
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.uri_match_fn = httpd_uri_match_wildcard;
    hc.lru_purge_enable = true;
    hc.stack_size = 6144;
    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &hc), TAG, "httpd");
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = h_index},
        {.uri = "/api/list", .method = HTTP_GET, .handler = h_list},
        {.uri = "/api/status", .method = HTTP_GET, .handler = h_status},
        {.uri = "/api/play", .method = HTTP_POST, .handler = h_play},
        {.uri = "/api/stop", .method = HTTP_POST, .handler = h_stop},
        {.uri = "/api/exit", .method = HTTP_POST, .handler = h_exit},
        {.uri = "/rec/*", .method = HTTP_GET, .handler = h_file},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &uris[i]), TAG, "uri %s", uris[i].uri);
    }
    return ESP_OK;
}

esp_err_t gb_web_start(const rg_wifi_cfg_t *cfg)
{
    ESP_RETURN_ON_FALSE(!s_play_q, ESP_ERR_INVALID_STATE, TAG, "already started");
    s_play_q = xQueueCreate(1, sizeof(uint32_t));
    ESP_RETURN_ON_FALSE(s_play_q, ESP_ERR_NO_MEM, TAG, "queue");
    ESP_RETURN_ON_FALSE(xTaskCreate(player_task, "gb_player", PLAYER_STACK, NULL, PLAYER_PRIO, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "player");
    ESP_RETURN_ON_ERROR(start_ap(cfg), TAG, "access point");
    ESP_RETURN_ON_ERROR(start_http(), TAG, "web server");
    ESP_LOGI(TAG, "listening mode: SSID \"%s\" (WPA2), http://192.168.4.1", cfg->ssid);
    return ESP_OK;
}

void gb_web_status(gb_web_status_t *status)
{
    uint32_t last = s_last_req_ms;
    status->playing = s_playing;
    status->clients = s_clients;
    status->last_request_us = last ? (int64_t)last * 1000 : 0;
    status->exit_requested = s_exit;
}

void gb_web_stop_playback(uint32_t timeout_ms)
{
    if (!s_play_q) {
        return;
    }
    xQueueReset(s_play_q);
    s_stop = true;
    for (uint32_t waited = 0; s_playing && waited < timeout_ms; waited += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
