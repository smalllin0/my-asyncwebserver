# AsyncWebServer 响应类型使用手册

> 本文档面向使用本异步 HTTP 框架的业务代码作者。
> 所有响应类共享同一套生命周期协议：`respond()` 为 `final`，子类只需实现 `pump()`（普通 body）或 `doRespond()`（特殊流程）。
> 选择哪个类，取决于"body 在哪、长度是否已知、是否需要模板/压缩"。

---

## 1. 架构总览

```
              ┌───────────────────────────────┐
              │  AsyncWebServerRequest        │
              │  req_->append / flush / close │
              │  req_->OnDrain(handler, ctx)  │
              └──────────────▲────────────────┘
                             │ 唯一交互面
        ┌────────────────────┴─────────────────────┐
        │       AsyncWebServerResponse             │
        │  respond(req)  final                     │
        │    ├─ sourceValid() ?                    │
        │    ├─ assembleHead()                     │
        │    ├─ doRespond()   ← 默认/特殊          │
        │    └─ OnDrain → pump() → flush()         │
        └──────────────────────────────────────────┘
                             │ 派生
   ┌──────────┬──────────────┼──────────┬───────────────┐
   │          │              │          │               │
 Basic   Progmem          File     Callback        Chunked
(字符串)  (静态二进制)    (文件流)  (定长回调)     (不定长回调)
                                            └─ Abstract (模板 + chunked)
                                            └─ WebSocket (101 升级)
```

**核心协议**

| 项 | 规则 |
|---|---|
| `respond(req)` | 基类 `final`，禁止覆盖 |
| `doRespond()` | 默认流程：写 header → 挂 `OnDrain` → 首次 `pump()` → `flush()`；WebSocket override |
| `pump()` | 每次只追加**一块** body，完成置 `done_ = true`；`OnDrain` 触发时再次调用 |
| `sourceValid()` | 构造失败时返回 `false`，`respond()` 自动 `finishResponse()` |
| buffer | 一次分配，全程复用 |
| 写数据 | 只用 `req_->append / flush / close`，禁止直接碰 `conn_` |
| 结束 | `done_ = true` → 下一次 `OnDrain` 走 `finish()` |

---

## 2. 类型速查

| 类 | 典型场景 | 长度 | Content-Length | 内存峰值 |
|---|---|---|---|---|
| `AsyncBasicResponse` | 小文本 / JSON / HTML 片段 | 已知 | 有 | 内容长度 + 一次 COPY |
| `AsyncProgmemResponse` | 嵌入式 gz 资源、404、favicon | 已知 | 有 | 0（零拷贝） |
| `AsyncFileResponse` | 静态文件（HTML/CSS/JS/图片） | 已知 | 有 | 1 块（≤1460B） |
| `AsyncCallbackResponse` | 长度已知、逐块产出 | 已知 | 有 / 无 | 1 块（≤1460B） |
| `AsyncChunkedResponse` | SSE、日志流、不定长输出 | 未知 | 无（chunked） | 1 块（~1032B） |
| `AsyncAbstractResponse` | 带 `%name%` 模板替换的流 | 未知 | 无（chunked） | 1 块 + 输出拷贝 |
| `AsyncWebSocketResponse` | WebSocket 协议升级 | — | 无 | header 字符串 |

---

## 3. 各类型使用示例

### 3.1 AsyncBasicResponse

**适用**：内容已在内存中的短响应（< 几 KB），不想额外加头。

```cpp
// main/sample_basic.cc
#include "AsyncWebServer.h"
#include "request/AsyncWebServerRequest.h"

void RegisterBasicRoutes(AsyncWebServer& server)
{
    // ① 最简：一行发送
    server.on("/ping", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", "pong");
    });

    // ② 带自定义头
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        const char* body = R"({"status":"ok","uptime":12345})";
        auto* r = req->beginResponse(200, "application/json", body);
        r->addHeader("Cache-Control", "no-store");
        req->send(r);
    });

    // ③ 错误响应
    server.on("/api/error", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(500, "text/plain", "internal error");
    });
}
```

**注意**
- 构造函数里默认加了 `Connection: close`。
- `content_` 是成员 `std::string`，`pump()` 里带 `TCP_WRITE_FLAG_COPY`，`append` 返回后即可复用字符串存储。

---

### 3.2 AsyncProgmemResponse

**适用**：资源已嵌入 Flash（`_binary_*_start/_end`），零堆分配。

**前置条件**：在 `CMakeLists.txt` 里嵌入文件

```cmake
# CMakeLists.txt 片段
idf_component_register(
    SRCS "main.cc" "sample_progmem.cc"
    EMBED_TXTFILES "web/404.html.gz"
)
```

```cpp
// main/sample_progmem.cc
#include "AsyncWebServer.h"
#include "request/AsyncWebServerRequest.h"

extern const char page404_gz[]     asm("_binary_404_html_gz_start");
extern const char page404_gz_end[] asm("_binary_404_html_gz_end");

static inline size_t page404_gz_len() {
    return static_cast<size_t>(page404_gz_end - page404_gz);
}

void RegisterProgmemRoutes(AsyncWebServer& server)
{
    // 默认 404（gzip）
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send_P(404, "text/html",
                    reinterpret_cast<const uint8_t*>(page404_gz),
                    page404_gz_len(),
                    "gzip");
    });

    // 无 Content-Encoding 版本
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* req) {
        extern const char fav[]     asm("_binary_favicon_ico_start");
        extern const char fav_end[] asm("_binary_favicon_ico_end");
        req->send_P(200, "image/x-icon",
                    reinterpret_cast<const uint8_t*>(fav),
                    static_cast<size_t>(fav_end - fav));
    });
}
```

**注意**
- `pump()` 里 `flags = 0`，**不拷贝**；progmem 指针永远有效。
- `send_P` 的两参版本不带编码，五参版本（`code, ct, ptr, len, encoding`）会加 `Content-Encoding`。

---

### 3.3 AsyncFileResponse

**适用**：从 SPIFFS / LittleFS / SD 提供静态文件；自动 `.gz` 探测。

```cpp
// main/sample_file.cc
#include "AsyncWebServer.h"
#include "request/AsyncWebServerRequest.h"

void RegisterFileRoutes(AsyncWebServer& server)
{
    // ① 直接发送（自动探测 .gz）
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send("/www/index.html");
    });

    // ② 带 MIME、下载
    server.on("/report.csv", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send("/data/report.csv", "text/csv", /*download=*/true);
    });

    // ③ 手工构造，添加缓存头
    server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* r = req->beginResponse("/www/app.js", "application/javascript");
        if (!r) { req->send(404); return; }
        r->addHeader("Cache-Control", "max-age=3600");
        req->send(r);
    });

    // ④ 简单静态目录
    server.serveStatic("/static/", "/www/static/", "max-age=86400");
}
```

**注意**
- `send(path)` 的第三个参数 `download` 为 `true` 时**不会**尝试 `.gz`。
- 打不开文件时 `beginResponse` 返回 `nullptr`，业务自己回 404/500。
- `buffer_` 1460B，一次分配全程复用；最后一块 `flags = 0` 触发 PSH。

---

### 3.4 AsyncCallbackResponse

**适用**：响应体运行时生成、**总长度已知**（能填 `Content-Length`）。

```cpp
// main/sample_callback.cc
#include "AsyncWebServer.h"
#include "request/AsyncWebServerRequest.h"
#include <cstdio>
#include <cstring>

static constexpr size_t kTotalSize = 1024 * 1024;

void RegisterCallbackRoutes(AsyncWebServer& server)
{
    server.on("/bin/blob", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* r = req->beginResponse("application/octet-stream", kTotalSize,
            [](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                if (index >= kTotalSize) return 0;
                size_t n = (maxLen < kTotalSize - index)
                         ? maxLen : (kTotalSize - index);
                for (size_t i = 0; i < n; ++i) {
                    buf[i] = static_cast<uint8_t>((index + i) * 31);
                }
                return n;
            });
        if (!r) { req->send(500); return; }
        req->send(r);
    });
}
```

**注意**
- `len == 0` 时退化为"边发边关"，不写 `Content-Length`。
- `filler` 返回 `RESPONSE_TRY_AGAIN` 表示"暂无数据"，`pump()` 直接返回，等下一次 `OnDrain`。
- `index` 语义是"已发送字节数"。

---

### 3.5 AsyncChunkedResponse

**适用**：总长度未知；SSE；持续推送。

```cpp
// main/sample_chunked.cc
#include "AsyncWebServer.h"
#include "request/AsyncWebServerRequest.h"
#include <cstdio>
#include <cstring>

static volatile uint32_t g_tick = 0;

void RegisterChunkedRoutes(AsyncWebServer& server)
{
    // SSE 示例
    server.on("/events", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->sendChunked("text/event-stream",
            [](uint8_t* buf, size_t maxLen, size_t /*index*/) -> size_t {
                int n = snprintf(reinterpret_cast<char*>(buf), maxLen,
                                 "event: tick\ndata: %u\n\n",
                                 static_cast<unsigned>(g_tick++));
                return n > 0 ? static_cast<size_t>(n) : 0;
            });
    });

    // 不定长日志流
    server.on("/logs", HTTP_GET, [](AsyncWebServerRequest* req) {
        static const char* lines[] = {
            "[INFO] boot\n",
            "[INFO] wifi connected\n",
            "[INFO] server ready\n",
        };
        static size_t idx = 0;
        idx = 0;
        req->sendChunked("text/plain",
            [](uint8_t* buf, size_t maxLen, size_t /*index*/) -> size_t {
                if (idx >= sizeof(lines) / sizeof(lines[0])) return 0;
                const char* s = lines[idx++];
                size_t n = strlen(s);
                if (n > maxLen) n = maxLen;
                memcpy(buf, s, n);
                return n;
            });
    });
}
```

**注意**
- HTTP/1.0 客户端会被 `beginChunkedResponse()` 自动回退到 `AsyncCallbackResponse(len=0)`。
- 每块带 `XXXX\r\n` 帧头 + `\r\n` 帧尾，`buffer_` 大小 1024+8，全程复用。
- 生成器返回 `0` 时，框架自动写终止块 `0\r\n\r\n` 并置 `done_`。

---

### 3.6 AsyncAbstractResponse

**适用**：需要在流式输出里做 `%name%` 模板替换。

```cpp
// main/sample_template.cc
#include "AsyncWebServer.h"
#include "request/AsyncWebServerRequest.h"
#include "response/AsyncAbstractResponse.h"
#include <cstring>

class MemoryTemplateResponse : public AsyncAbstractResponse {
public:
    MemoryTemplateResponse(const char* data, size_t len)
        : AsyncAbstractResponse(
              [](const std::string& name) -> std::string {
                  if (name == "title")  return "Async Demo";
                  if (name == "uptime") return "12345";
                  return "";
              })
        , data_(data)
        , total_(len)
    {
        code_ = 200;
        contentType_ = "text/html";
    }
    bool sourceValid() const override { return data_ != nullptr; }

protected:
    size_t fillBuffer(uint8_t* buf, size_t maxLen) override {
        if (offset_ >= total_) return 0;
        size_t n = (maxLen < total_ - offset_) ? maxLen : (total_ - offset_);
        memcpy(buf, data_ + offset_, n);
        offset_ += n;
        return n;
    }

private:
    const char* data_;
    size_t      total_;
    size_t      offset_{0};
};

void RegisterTemplateRoutes(AsyncWebServer& server)
{
    server.on("/page", HTTP_GET, [](AsyncWebServerRequest* req) {
        static const char tpl[] =
            "<html><body>"
            "<h1>%title%</h1>"
            "<p>uptime: %uptime%</p>"
            "</body></html>";
        auto* r = new MemoryTemplateResponse(tpl, strlen(tpl));
        req->send(r);
    });
}
```

**注意**
- 简化版 `processTemplates` **不支持跨块的模板名**（`%name%` 必须完整落在一个 1024B 块内）。
- 每块会临时构造 `std::vector<uint8_t> framed`，容量会被 vector 复用。

---

### 3.7 AsyncWebSocketResponse

**适用**：框架内部使用，业务代码**不直接构造**。

```cpp
// main/sample_websocket.cc
#include "AsyncWebServer.h"
#include "AsyncWebSocket.h"

void RegisterWebSocket(AsyncWebServer& server)
{
    static AsyncWebSocket ws("/ws");

    ws.onEvent([](AsyncWebSocket* /*srv*/,
                  AsyncWebSocketClient* client,
                  AwsEventType type,
                  void* arg,
                  uint8_t* data,
                  size_t len) {
        switch (type) {
          case WS_EVT_CONNECT:
            break;
          case WS_EVT_DATA: {
            ws.text(client->id(), std::string((char*)data, len));
            break;
          }
          case WS_EVT_DISCONNECT:
          case WS_EVT_ERROR:
          case WS_EVT_PONG:
            break;
        }
    });

    server.addHandler(&ws);
}
```

**内部行为**
- `AsyncWebSocketResponse::doRespond()` 覆盖基类流程：
  1. 写 101 header（`Connection: Upgrade`、`Upgrade: websocket`、`Sec-WebSocket-Accept`）
  2. 挂一次性 `OnDrain`，构造 `AsyncWebSocketClient` 接管后续帧
  3. `state_ = RESPONSE_END`，**不调用 `finish()`**
- 后续数据完全由 `AsyncWebSocketClient` 管理，不再走 HTTP response 协议。

**注意**
- 业务侧不要 `delete` 或手工 `new AsyncWebSocketResponse`。
- `AsyncWebSocket` 实例必须是长生命周期（静态或 server 成员）。

---

## 4. 决策树

```
发 WebSocket 升级？
├─ 是 → AsyncWebSocketResponse（框架内部）
└─ 否 ↓

内容在内存里？
├─ 是，且是只读静态（progmem/flash）
│   ├─ 需要 Content-Encoding → send_P(code, ct, ptr, len, "gzip")
│   └─ 不需要              → send_P(code, ct, ptr, len)
├─ 是，且是普通 std::string
│   └─ AsyncBasicResponse（send / beginResponse）
└─ 否 ↓

在文件系统里？
├─ 是 → AsyncFileResponse
└─ 否 ↓

运行时生成？
├─ 总长度已知 → AsyncCallbackResponse（带 len）
├─ 总长度未知
│   ├─ 需要模板替换 → AsyncAbstractResponse 派生
│   └─ 不需要       → AsyncChunkedResponse
└─
```

---

## 5. 自定义响应模板

### 5.1 普通短 body

```cpp
// main/custom_json_response.cc
#include "response/AsyncWebServerResponse.h"
#include "request/AsyncWebServerRequest.h"
#include <string>

class CustomJsonResponse : public AsyncWebServerResponse {
public:
    explicit CustomJsonResponse(std::string body)
        : AsyncWebServerResponse(200, "application/json")
        , body_(std::move(body))
    {
        contentLength_ = body_.size();
    }
    bool sourceValid() const override { return true; }

protected:
    void pump() override {
        if (done_) return;
        req_->append(body_.c_str(), body_.length(), TCP_WRITE_FLAG_COPY);
        done_ = true;
    }

private:
    std::string body_;
};
```

### 5.2 流式，长度未知

```cpp
// main/custom_stream_response.cc
#include "response/AsyncWebServerResponse.h"
#include "request/AsyncWebServerRequest.h"
#include <cstdlib>
#include <cstring>

class CustomStreamResponse : public AsyncWebServerResponse {
public:
    using NextFn = size_t (*)(uint8_t* buf, size_t maxLen, void* ctx);

    CustomStreamResponse(const char* contentType, NextFn next, void* ctx)
        : AsyncWebServerResponse(200, contentType)
        , next_(next)
        , ctx_(ctx)
    {
        sendContentLength_ = false;
        chunked_ = true;
    }
    ~CustomStreamResponse() override {
        if (buf_) { free(buf_); buf_ = nullptr; }
    }
    bool sourceValid() const override { return next_ != nullptr; }

protected:
    void pump() override {
        constexpr size_t kChunk = 1024;
        if (done_) return;

        if (!buf_) {
            buf_ = static_cast<uint8_t*>(malloc(kChunk + 8));
            if (!buf_) return;
        }

        size_t n = next_(buf_ + 6, kChunk, ctx_);
        if (n == 0) {
            static const char term[] = "0\r\n\r\n";
            req_->append(term, 5, TCP_WRITE_FLAG_MORE);
            done_ = true;
            return;
        }

        static const char hex[] = "0123456789abcdef";
        buf_[0] = hex[(n >> 12) & 0xF];
        buf_[1] = hex[(n >>  8) & 0xF];
        buf_[2] = hex[(n >>  4) & 0xF];
        buf_[3] = hex[(n      ) & 0xF];
        buf_[4] = '\r';
        buf_[5] = '\n';
        buf_[n + 6] = '\r';
        buf_[n + 7] = '\n';

        if (!req_->append(buf_, n + 8, TCP_WRITE_FLAG_MORE)) return;
    }

private:
    NextFn   next_;
    void*    ctx_;
    uint8_t* buf_{nullptr};
};
```

**必守契约**

1. 构造函数里设置 `code_ / contentType_ / sendContentLength_ / chunked_`
2. `sourceValid()` 返回 `false` 时框架放弃此 response
3. `pump()` **每次只 append 一块**；`done_` 未置位就等 `OnDrain` 驱动下一块
4. 成员 buffer 在析构里 `free`
5. **不要**写 `respond()`

---

## 6. 常见坑

### 6.1 `Content-Length` vs `chunked`

- 有 `Content-Length` 的响应，**必须**严格等于实际发送字节数
- `AsyncChunkedResponse` 不写 `Content-Length`，由 `Transfer-Encoding: chunked` 表达边界
- `AsyncCallbackResponse` 传 `len = 0` 时退化为"边发边关"

### 6.2 单 buffer 复用

- `pump()` 内 `buffer_` 一旦分配，**整个响应期间只有这一块**
- 每次 `append()` 后**必须 return**，把下一块交给 `OnDrain`
- 在 `pump()` 里连续 `append` 两次 → 发送池同时持有两个指向同一 buffer 的引用 → 数据错乱

### 6.3 `TCP_WRITE_FLAG_COPY`

- 带 `COPY`：`AsyncConnection` 内部拷贝，`append` 返回后 `buffer_` 立即可复用
- 不带 `COPY`：`buffer_` 必须活到 `OnDrain`
- 本框架推荐：`pump()` 里统一**不带 COPY**，靠 `OnDrain` 驱动；`AsyncBasicResponse` 的 `content_` 是成员变量，加 COPY 是为了保守安全

### 6.4 `RESPONSE_TRY_AGAIN`

- `filler` 返回 `RESPONSE_TRY_AGAIN` → `pump()` 直接 `return`，不写字节
- 框架不会主动重试，下次 `OnDrain` 才会再来
- 若数据源长时间无数据会卡住 → 在数据源侧主动 `req->flush()` 唤醒（进阶用法）

### 6.5 `finish()` 顺序

```cpp
req_->close();             // 发 FIN
req_->finishResponse();    // 清 OnDrain + delete response
```

- `close()` 先于 `delete response`，因为 `close` 内部可能仍需 `conn_`
- `finishResponse()` 会把 `OnDrain` 置空，防止 response 析构后回调悬空

### 6.6 `beginResponse()` 返回 `nullptr`

- 文件不存在、`callback` 为空 → 返回 `nullptr`
- 直接 `req->send(nullptr)` 会走 `close + onDisconnect`，**不会**发 500
- 规范写法：

```cpp
auto* r = req->beginResponse(path);
req->send(r ? r : req->beginResponse(500, "text/plain", "internal"));
```

---

## 7. 快速参考

| 需求 | 一行写法 |
|---|---|
| 短文本 | `req->send(200, "text/plain", "hi")` |
| JSON | `req->send(req->beginResponse(200, "application/json", js))` |
| 静态文件（自动 gz） | `req->send("/www/index.html")` |
| 下载文件 | `req->send(path, "application/octet-stream", true)` |
| 嵌入式 gz 资源 | `req->send_P(404, "text/html", ptr, len, "gzip")` |
| 已知长度流 | `req->send(req->beginResponse(ct, len, filler))` |
| 未知长度流 / SSE | `req->sendChunked("text/event-stream", filler)` |
| WebSocket | `ws.onEvent(...); server.addHandler(&ws)` |