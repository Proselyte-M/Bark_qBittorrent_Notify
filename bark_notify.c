#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

// --- Configuration ---
#ifndef DEFAULT_BARK_KEY
#define DEFAULT_BARK_KEY "YOUR_DEFAULT_KEY"
#endif

#define USER_AGENT L"qBt-Bark-Notifier/1.0"
#define TIMEOUT_MS 5000

// --- Globals ---
FILE *log_fp = NULL;

void log_info(const char *fmt, ...) {
    if (!log_fp) return;
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(log_fp, "[%04d-%02d-%02d %02d:%02d:%02d] ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    
    va_list args;
    va_start(args, fmt);
    vfprintf(log_fp, fmt, args);
    va_end(args);
    
    fprintf(log_fp, "\n");
    fflush(log_fp);
}

typedef struct {
    char *name;         // %N
    char *category;     // %L
    char *tags;         // %G
    char *content_path; // %F
    char *root_path;    // %R
    char *save_path;    // %D
    char *file_count;   // %C
    char *size;         // %Z
    char *tracker;      // %T
    char *info_hash_v1; // %I
    char *info_hash_v2; // %J
    char *torrent_id;   // %K
    char *event;        // "completed" or "start"
    char *bark_key;
    char *bark_server;
} NotificationParams;

// --- Helper Functions ---

// Convert size in bytes to human readable string (GiB, MiB, etc.)
void format_size(const char *bytes_str, char *out, size_t out_size) {
    if (!bytes_str || strcmp(bytes_str, "-") == 0) {
        snprintf(out, out_size, "-");
        return;
    }

    long long bytes = strtoll(bytes_str, NULL, 10);
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int i = 0;
    double size = (double)bytes;

    while (size >= 1024 && i < 5) {
        size /= 1024;
        i++;
    }

    snprintf(out, out_size, "%.2f %s", size, units[i]);
}

// URL Encode string
char *url_encode(const char *str) {
    if (!str) return _strdup("");
    size_t len = strlen(str);
    char *encoded = (char *)malloc(len * 3 + 1);
    if (!encoded) return NULL;

    char *p = encoded;
    while (*str) {
        if (isalnum((unsigned char)*str) || *str == '-' || *str == '_' || *str == '.' || *str == '~') {
            *p++ = *str;
        } else {
            sprintf(p, "%%%02X", (unsigned char)*str);
            p += 3;
        }
        str++;
    }
    *p = '\0';
    return encoded;
}

// Convert UTF-8 char* to WideChar (for WinHTTP)
wchar_t* utf8_to_wide(const char* str) {
    if (!str) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    if (len == 0) return NULL;
    wchar_t* wstr = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!wstr) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len);
    return wstr;
}

// Convert WideChar to UTF-8 char*
char* wide_to_utf8(const wchar_t* wstr) {
    if (!wstr) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len == 0) return NULL;
    char* str = (char*)malloc(len);
    if (!str) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
    return str;
}

// Check if string is missing or empty or placeholder
int is_valid(const char *s) {
    return s && *s && strcmp(s, "-") != 0;
}

void trim_inplace(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[len - 1] = '\0';
        len--;
    }
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
}

int load_ini_config(const wchar_t *path, char *out_key, size_t key_size, char *out_server, size_t server_size, int *found_key, int *found_server) {
    FILE *fp = _wfopen(path, L"rb");
    if (!fp) {
        log_info("错误：无法打开配置文件");
        return 0;
    }

    char line[1024];
    int in_bark = 0;
    int first_line = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (first_line) {
            if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
                memmove(line, line + 3, strlen(line + 3) + 1);
                log_info("检测到 UTF-8 BOM，已忽略");
            }
            first_line = 0;
        }

        trim_inplace(line);
        if (line[0] == '\0') continue;
        if (line[0] == ';' || line[0] == '#') continue;

        size_t len = strlen(line);
        if (line[0] == '[' && line[len - 1] == ']') {
            line[len - 1] = '\0';
            char *section = line + 1;
            trim_inplace(section);
            in_bark = (_stricmp(section, "Bark") == 0);
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim_inplace(key);
        trim_inplace(val);

        if (in_bark) {
            if (_stricmp(key, "Key") == 0) {
                strncpy_s(out_key, key_size, val, _TRUNCATE);
                *found_key = 1;
            } else if (_stricmp(key, "Server") == 0) {
                strncpy_s(out_server, server_size, val, _TRUNCATE);
                *found_server = 1;
            }
        }
    }

    fclose(fp);
    return 1;
}

// Send Notification via WinHTTP
int send_bark_notification(const char *server, const char *key, const char *title, const char *body, const char *group, const char *level) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    int result = 0;
    
    // Parse Server URL (Simple parsing, assumes hostname or hostname:port)
    // Default to api.day.app if not specified fully
    
    // Prepare URLs
    char url_path[512];
    // Fix: Remove trailing slash to avoid 404 on some Bark servers
    snprintf(url_path, sizeof(url_path), "/%s", key); 

    wchar_t *w_server = utf8_to_wide(server);
    wchar_t *w_path = utf8_to_wide(url_path);

    // Initialize WinHTTP
    log_info("初始化 WinHTTP...");
    hSession = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        log_info("错误：WinHttpOpen 失败，错误码：%d", GetLastError());
        goto cleanup;
    }

    // Set Timeouts (5s)
    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    // Connect
    // Handle port if present in server string
    wchar_t *port_ptr = wcschr(w_server, L':');
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    if (port_ptr) {
        *port_ptr = 0; // split host and port
        port = (INTERNET_PORT)_wtoi(port_ptr + 1);
    }
    
    log_info("连接服务器：%s，端口：%d", server, port);
    hConnect = WinHttpConnect(hSession, w_server, port, 0);
    if (!hConnect) {
        log_info("错误：WinHttpConnect 失败，错误码：%d", GetLastError());
        goto cleanup;
    }

    // Open Request
    log_info("打开请求路径：%s", url_path);
    hRequest = WinHttpOpenRequest(hConnect, L"POST", w_path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        log_info("错误：WinHttpOpenRequest 失败，错误码：%d", GetLastError());
        goto cleanup;
    }

    // Prepare POST data
    char *enc_title = url_encode(title);
    char *enc_body = url_encode(body);
    char *enc_group = url_encode(group);
    char *enc_level = url_encode(level);
    char *enc_icon = url_encode("https://raw.githubusercontent.com/qbittorrent/qBittorrent/master/src/icons/qbittorrent.ico");

    char *post_data = (char *)malloc(strlen(enc_title) + strlen(enc_body) + strlen(enc_group) + strlen(enc_level) + strlen(enc_icon) + 256);
    if (post_data) {
        sprintf(post_data, "title=%s&body=%s&group=%s&level=%s&icon=%s", enc_title, enc_body, enc_group, enc_level, enc_icon);
        
        log_info("发送请求中，数据长度：%d", strlen(post_data));
        const wchar_t *headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
        BOOL sent = WinHttpSendRequest(hRequest, headers, (DWORD)-1L, post_data, (DWORD)strlen(post_data), (DWORD)strlen(post_data), 0);
        
        if (sent) {
             if (WinHttpReceiveResponse(hRequest, NULL)) {
                 DWORD statusCode = 0;
                 DWORD dwSize = sizeof(statusCode);
                 WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
                 log_info("收到响应，HTTP 状态码：%d", statusCode);
                 if (statusCode >= 200 && statusCode < 300) {
                     result = 1; // Success
                     log_info("通知发送成功");
                 } else {
                     log_info("错误：服务端返回非成功状态码");
                 }
             } else {
                 log_info("错误：WinHttpReceiveResponse 失败，错误码：%d", GetLastError());
             }
        } else {
            log_info("错误：WinHttpSendRequest 失败，错误码：%d", GetLastError());
        }
        free(post_data);
    } else {
        log_info("错误：post_data 内存分配失败");
    }
    
    free(enc_title); free(enc_body); free(enc_group); free(enc_level); free(enc_icon);

cleanup:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    free(w_server);
    free(w_path);
    
    return result;
}

// Entry point for Windows GUI application (No Console)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    // Initialize Logging
    wchar_t log_path[MAX_PATH];
    if (GetModuleFileNameW(NULL, log_path, MAX_PATH)) {
        wchar_t *dot = wcsrchr(log_path, L'.');
        if (dot) *dot = L'\0';
        wcscat(log_path, L".log");
    } else {
        wcscpy(log_path, L".\\bark_notify.log");
    }

    log_fp = _wfopen(log_path, L"a");
    if (log_fp) {
        log_info("--- 程序启动 ---");
        log_info("日志文件：%S", log_path);
    }

    NotificationParams params = {0};
    
    // Load config from INI file
    wchar_t ini_path[MAX_PATH];
    if (GetModuleFileNameW(NULL, ini_path, MAX_PATH)) {
        wchar_t *dot = wcsrchr(ini_path, L'.');
        if (dot) *dot = L'\0';
        wcscat(ini_path, L".ini");
    } else {
        // Fallback to local file if GetModuleFileName fails (unlikely)
        wcscpy(ini_path, L".\\bark_notify.ini");
    }
    
    log_info("读取配置文件：%S", ini_path);
    DWORD ini_attrs = GetFileAttributesW(ini_path);
    if (ini_attrs == INVALID_FILE_ATTRIBUTES) {
        log_info("配置文件不存在");
    } else {
        log_info("配置文件已找到");
    }

    char ini_key[256] = {0};
    char ini_server[256] = {0};
    int found_key = 0;
    int found_server = 0;

    if (load_ini_config(ini_path, ini_key, sizeof(ini_key), ini_server, sizeof(ini_server), &found_key, &found_server)) {
        log_info("配置解析完成 - 服务器：%s，Key：%s", 
                 (found_server && *ini_server) ? ini_server : "默认值",
                 (found_key && *ini_key) ? "已找到" : "未找到");
    } else {
        log_info("配置解析失败，使用默认值");
    }

    // Prioritize INI, then Fallback to Macro/Default
    if (found_key && *ini_key) {
        params.bark_key = ini_key;
    } else {
        params.bark_key = DEFAULT_BARK_KEY;
    }

    if (found_server && *ini_server) {
        params.bark_server = ini_server;
    } else {
        params.bark_server = "api.day.app";
    }

    // Defaults for placeholders
    params.name = "-";
    params.category = "-";
    params.tags = "-";
    params.save_path = "-";
    params.size = "-";
    params.tracker = "-";
    params.info_hash_v1 = "-";
    params.info_hash_v2 = "-";
    params.torrent_id = "-";
    params.content_path = "-";
    params.root_path = "-";
    params.file_count = "-";
    params.event = "Notification"; // default

    // Argument Parsing using CommandLineToArgvW for Unicode support
    int argc;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) return 1;

    // Convert all args to UTF-8
    char **argv = (char**)malloc(argc * sizeof(char*));
    for (int i = 0; i < argc; i++) {
        argv[i] = wide_to_utf8(wargv[i]);
    }

    // Manual Flag Parsing
    // Map: N=name, L=category, G=tags, F=content_path, R=root, D=save_path, 
    // C=count, Z=size, T=tracker, I=v1, J=v2, K=id, E=event
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            char flag = argv[i][1];
            if (i + 1 < argc) {
                char *val = argv[i+1];
                switch (flag) {
                    case 'N': params.name = val; break;
                    case 'L': params.category = val; break;
                    case 'G': params.tags = val; break;
                    case 'F': params.content_path = val; break;
                    case 'R': params.root_path = val; break;
                    case 'D': params.save_path = val; break;
                    case 'C': params.file_count = val; break;
                    case 'Z': params.size = val; break;
                    case 'T': params.tracker = val; break;
                    case 'I': params.info_hash_v1 = val; break;
                    case 'J': params.info_hash_v2 = val; break;
                    case 'K': params.torrent_id = val; break;
                    case 'E': params.event = val; break;
                }
                i++; // Skip value
            }
        }
    }
    
    log_info("参数解析完成：事件=%s，名称=%s，分类=%s，标签=%s，保存路径=%s，大小=%s", 
             params.event, params.name, params.category, params.tags, params.save_path, params.size);
    
    // Construct Body
    char size_formatted[64];
    format_size(params.size, size_formatted, sizeof(size_formatted));
    
    // Title
    char title[256];
    snprintf(title, sizeof(title), "qBittorrent %s", strcmp(params.event, "start") == 0 ? "Started" : "Finished");
    
    // Body - Main info
    char body[4096];
    int offset = 0;
    
    offset += snprintf(body + offset, sizeof(body) - offset, 
        "Event: %s\nName: %s\nCategory: %s\nTags: %s\nPath: %s\nSize: %s",
        strcmp(params.event, "start") == 0 ? "Download Started" : "Download Completed",
        params.name, params.category, params.tags, params.save_path, size_formatted
    );
    
    // Body - Optional info (folded)
    // Only add if they are valid (not "-")
    char optional[2048] = "";
    int opt_off = 0;
    
    if (is_valid(params.tracker)) opt_off += snprintf(optional + opt_off, sizeof(optional) - opt_off, "Tracker: %s | ", params.tracker);
    if (is_valid(params.info_hash_v1)) opt_off += snprintf(optional + opt_off, sizeof(optional) - opt_off, "Hash v1: %s | ", params.info_hash_v1);
    if (is_valid(params.info_hash_v2)) opt_off += snprintf(optional + opt_off, sizeof(optional) - opt_off, "Hash v2: %s | ", params.info_hash_v2);
    
    if (opt_off > 0) {
        // Remove trailing " | "
        optional[opt_off - 3] = '\0';
        offset += snprintf(body + offset, sizeof(body) - offset, "\n%s", optional);
    }
    
    int ret = send_bark_notification(params.bark_server, params.bark_key, title, body, "qBittorrent", "active");

    // Cleanup
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    LocalFree(wargv);
    
    log_info("程序退出，返回码：%d", ret ? 0 : 1);
    if (log_fp) fclose(log_fp);

    return ret ? 0 : 1;
}
