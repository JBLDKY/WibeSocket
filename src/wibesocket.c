#include "ultraws.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_FRAME_HEADER_SIZE 14
#define WS_MASK_SIZE 4
#define WS_MAX_FRAME_SIZE (64 * 1024 * 1024) // 64MB max frame

typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
} ws_opcode_t;

typedef struct {
    uint8_t fin:1;
    uint8_t rsv1:1;
    uint8_t rsv2:1;
    uint8_t rsv3:1;
    uint8_t opcode:4;
    uint8_t mask:1;
    uint8_t payload_len:7;
} ws_frame_header_t;

struct ultraws {
    int fd;
    char *host;
    int port;
    char *path;
    char *key;
    uint8_t mask_key[WS_MASK_SIZE];
    uint8_t *recv_buffer;
    size_t recv_buffer_size;
    size_t recv_buffer_pos;
    int connected;
    int close_sent;
    int close_received;
};

// Base64 encoding function
static char* base64_encode(const unsigned char* input, int length) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    
    char *buff = (char *)malloc(bufferPtr->length + 1);
    memcpy(buff, bufferPtr->data, bufferPtr->length);
    buff[bufferPtr->length] = '\0';
    
    BIO_free_all(bio);
    return buff;
}

// Generate WebSocket key
static char* generate_ws_key() {
    unsigned char random_bytes[16];
    for (int i = 0; i < 16; i++) {
        random_bytes[i] = rand() % 256;
    }
    return base64_encode(random_bytes, 16);
}

// Parse URI
static int parse_uri(const char* uri, char** host, int* port, char** path) {
    char* uri_copy = strdup(uri);
    char* scheme = strtok(uri_copy, "://");
    if (!scheme || strcmp(scheme, "ws") != 0) {
        free(uri_copy);
        return -1;
    }
    
    char* rest = strtok(NULL, "://");
    if (!rest) {
        free(uri_copy);
        return -1;
    }
    
    char* host_port = strtok(rest, "/");
    if (!host_port) {
        free(uri_copy);
        return -1;
    }
    
    char* colon = strchr(host_port, ':');
    if (colon) {
        *colon = '\0';
        *host = strdup(host_port);
        *port = atoi(colon + 1);
    } else {
        *host = strdup(host_port);
        *port = 80;
    }
    
    char* path_start = strchr(rest, '/');
    if (path_start) {
        *path = strdup(path_start);
    } else {
        *path = strdup("/");
    }
    
    free(uri_copy);
    return 0;
}

// Create WebSocket handshake
static char* create_handshake(const char* host, int port, const char* path, const char* key) {
    char* handshake = malloc(1024);
    snprintf(handshake, 1024,
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, port, key);
    return handshake;
}

// Verify handshake response
static int verify_handshake_response(const char* response, const char* expected_key) {
    char* accept_header = strstr(response, "Sec-WebSocket-Accept: ");
    if (!accept_header) return -1;
    
    accept_header += 22; // Skip "Sec-WebSocket-Accept: "
    char* end = strchr(accept_header, '\r');
    if (!end) return -1;
    *end = '\0';
    
    // Calculate expected accept value
    char concatenated[256];
    snprintf(concatenated, sizeof(concatenated), "%s%s", expected_key, WS_GUID);
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)concatenated, strlen(concatenated), hash);
    
    char* expected_accept = base64_encode(hash, SHA_DIGEST_LENGTH);
    int result = strcmp(accept_header, expected_accept);
    free(expected_accept);
    
    return result == 0 ? 0 : -1;
}

// Generate mask key
static void generate_mask_key(uint8_t* mask) {
    for (int i = 0; i < WS_MASK_SIZE; i++) {
        mask[i] = rand() % 256;
    }
}

// Apply mask to payload
static void apply_mask(uint8_t* data, size_t len, const uint8_t* mask) {
    for (size_t i = 0; i < len; i++) {
        data[i] ^= mask[i % WS_MASK_SIZE];
    }
}

// Create WebSocket frame
static int create_frame(uint8_t* buffer, size_t buffer_size, 
                       const void* payload, size_t payload_len,
                       ws_opcode_t opcode, int fin, const uint8_t* mask) {
    if (buffer_size < WS_FRAME_HEADER_SIZE + payload_len) return -1;
    
    ws_frame_header_t* header = (ws_frame_header_t*)buffer;
    header->fin = fin;
    header->rsv1 = 0;
    header->rsv2 = 0;
    header->rsv3 = 0;
    header->opcode = opcode;
    header->mask = mask ? 1 : 0;
    
    size_t header_size = 2;
    
    if (payload_len < 126) {
        header->payload_len = payload_len;
    } else if (payload_len < 65536) {
        header->payload_len = 126;
        buffer[2] = (payload_len >> 8) & 0xFF;
        buffer[3] = payload_len & 0xFF;
        header_size += 2;
    } else {
        header->payload_len = 127;
        for (int i = 0; i < 8; i++) {
            buffer[2 + i] = (payload_len >> (56 - i * 8)) & 0xFF;
        }
        header_size += 8;
    }
    
    if (mask) {
        memcpy(buffer + header_size, mask, WS_MASK_SIZE);
        header_size += WS_MASK_SIZE;
    }
    
    if (payload_len > 0) {
        memcpy(buffer + header_size, payload, payload_len);
        if (mask) {
            apply_mask(buffer + header_size, payload_len, mask);
        }
    }
    
    return header_size + payload_len;
}

// Parse WebSocket frame
static int parse_frame(const uint8_t* data, size_t data_len, 
                      ws_frame_header_t* header, const uint8_t** payload, 
                      size_t* payload_len, const uint8_t** mask) {
    if (data_len < 2) return -1;
    
    memcpy(header, data, 2);
    size_t pos = 2;
    
    // Get extended payload length
    uint64_t extended_len = header->payload_len;
    if (extended_len == 126) {
        if (data_len < pos + 2) return -1;
        extended_len = (data[pos] << 8) | data[pos + 1];
        pos += 2;
    } else if (extended_len == 127) {
        if (data_len < pos + 8) return -1;
        extended_len = 0;
        for (int i = 0; i < 8; i++) {
            extended_len = (extended_len << 8) | data[pos + i];
        }
        pos += 8;
    }
    
    // Get mask if present
    if (header->mask) {
        if (data_len < pos + WS_MASK_SIZE) return -1;
        *mask = data + pos;
        pos += WS_MASK_SIZE;
    } else {
        *mask = NULL;
    }
    
    // Check if we have enough data for payload
    if (data_len < pos + extended_len) return -1;
    
    *payload = data + pos;
    *payload_len = extended_len;
    
    return pos + extended_len;
}

ultraws_t* ultraws_connect(const char* uri) {
    ultraws_t* ws = calloc(1, sizeof(ultraws_t));
    if (!ws) return NULL;
    
    ws->fd = -1;
    ws->recv_buffer_size = 4096;
    ws->recv_buffer = malloc(ws->recv_buffer_size);
    if (!ws->recv_buffer) {
        free(ws);
        return NULL;
    }
    
    // Parse URI
    if (parse_uri(uri, &ws->host, &ws->port, &ws->path) != 0) {
        ultraws_close(ws);
        return NULL;
    }
    
    // Generate WebSocket key
    ws->key = generate_ws_key();
    if (!ws->key) {
        ultraws_close(ws);
        return NULL;
    }
    
    // Create socket
    ws->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ws->fd < 0) {
        ultraws_close(ws);
        return NULL;
    }
    
    // Resolve hostname
    struct hostent* host_entry = gethostbyname(ws->host);
    if (!host_entry) {
        ultraws_close(ws);
        return NULL;
    }
    
    // Connect
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ws->port);
    memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);
    
    if (connect(ws->fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        ultraws_close(ws);
        return NULL;
    }
    
    // Send handshake
    char* handshake = create_handshake(ws->host, ws->port, ws->path, ws->key);
    if (!handshake) {
        ultraws_close(ws);
        return NULL;
    }
    
    size_t handshake_len = strlen(handshake);
    ssize_t sent = send(ws->fd, handshake, handshake_len, 0);
    free(handshake);
    
    if (sent != handshake_len) {
        ultraws_close(ws);
        return NULL;
    }
    
    // Receive handshake response
    char response[4096];
    ssize_t received = recv(ws->fd, response, sizeof(response) - 1, 0);
    if (received <= 0) {
        ultraws_close(ws);
        return NULL;
    }
    response[received] = '\0';
    
    // Verify response
    if (verify_handshake_response(response, ws->key) != 0) {
        ultraws_close(ws);
        return NULL;
    }
    
    ws->connected = 1;
    return ws;
}

int ultraws_send(ultraws_t* ws, const void* data, size_t len) {
    if (!ws || !ws->connected || ws->close_sent) return -1;
    
    uint8_t frame_buffer[WS_FRAME_HEADER_SIZE + len];
    generate_mask_key(ws->mask_key);
    
    int frame_len = create_frame(frame_buffer, sizeof(frame_buffer), 
                                data, len, WS_OPCODE_BINARY, 1, ws->mask_key);
    if (frame_len < 0) return -1;
    
    ssize_t sent = send(ws->fd, frame_buffer, frame_len, 0);
    return sent == frame_len ? 0 : -1;
}

int ultraws_recv(ultraws_t* ws, void* buf, size_t buf_size) {
    if (!ws || !ws->connected) return -1;
    
    // Try to read from existing buffer first
    if (ws->recv_buffer_pos < ws->recv_buffer_size) {
        ws_frame_header_t header;
        const uint8_t* payload;
        size_t payload_len;
        const uint8_t* mask;
        
        int parsed = parse_frame(ws->recv_buffer + ws->recv_buffer_pos, 
                               ws->recv_buffer_size - ws->recv_buffer_pos,
                               &header, &payload, &payload_len, &mask);
        
        if (parsed > 0) {
            // Handle control frames
            if (header.opcode == WS_OPCODE_CLOSE) {
                ws->close_received = 1;
                return 0;
            } else if (header.opcode == WS_OPCODE_PING) {
                // Send pong response
                uint8_t pong_frame[WS_FRAME_HEADER_SIZE];
                create_frame(pong_frame, sizeof(pong_frame), 
                           payload, payload_len, WS_OPCODE_PONG, 1, NULL);
                send(ws->fd, pong_frame, WS_FRAME_HEADER_SIZE + payload_len, 0);
                ws->recv_buffer_pos += parsed;
                return ultraws_recv(ws, buf, buf_size); // Try next frame
            } else if (header.opcode == WS_OPCODE_PONG) {
                ws->recv_buffer_pos += parsed;
                return ultraws_recv(ws, buf, buf_size); // Try next frame
            }
            
            // Handle data frames
            if (payload_len <= buf_size) {
                memcpy(buf, payload, payload_len);
                if (mask) {
                    apply_mask((uint8_t*)buf, payload_len, mask);
                }
                ws->recv_buffer_pos += parsed;
                return payload_len;
            }
        }
    }
    
    // Read more data from socket
    ssize_t received = recv(ws->fd, ws->recv_buffer + ws->recv_buffer_pos, 
                           ws->recv_buffer_size - ws->recv_buffer_pos, 0);
    if (received <= 0) return -1;
    
    ws->recv_buffer_size += received;
    
    // Try parsing again
    return ultraws_recv(ws, buf, buf_size);
}

int ultraws_close(ultraws_t* ws) {
    if (!ws) return -1;
    
    if (ws->connected && !ws->close_sent) {
        // Send close frame
        uint8_t close_frame[WS_FRAME_HEADER_SIZE];
        create_frame(close_frame, sizeof(close_frame), 
                   NULL, 0, WS_OPCODE_CLOSE, 1, NULL);
        send(ws->fd, close_frame, WS_FRAME_HEADER_SIZE, 0);
        ws->close_sent = 1;
    }
    
    if (ws->fd >= 0) {
        close(ws->fd);
        ws->fd = -1;
    }
    
    free(ws->host);
    free(ws->path);
    free(ws->key);
    free(ws->recv_buffer);
    free(ws);
    
    return 0;
}

