#ifndef WIBESOCKET_H
#define WIBESOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque WebSocket connection handle
 * 
 * This type represents an active WebSocket connection. The internal
 * structure is hidden from the public API to maintain encapsulation
 * and allow for future implementation changes without breaking ABI.
 */
typedef struct wibesocket_conn wibesocket_conn_t;

/**
 * @brief WebSocket connection states
 */
typedef enum {
    WIBESOCKET_STATE_INIT = 0,      ///< Initial state, connection not established
    WIBESOCKET_STATE_CONNECTING,    ///< Connection handshake in progress
    WIBESOCKET_STATE_OPEN,          ///< Connection established and ready
    WIBESOCKET_STATE_CLOSING,       ///< Connection closing handshake in progress
    WIBESOCKET_STATE_CLOSED,        ///< Connection fully closed
    WIBESOCKET_STATE_ERROR          ///< Connection in error state
} wibesocket_state_t;

/**
 * @brief WebSocket frame types per RFC 6455
 */
typedef enum {
    WIBESOCKET_FRAME_CONTINUATION = 0x0,
    WIBESOCKET_FRAME_TEXT = 0x1,
    WIBESOCKET_FRAME_BINARY = 0x2,
    WIBESOCKET_FRAME_CLOSE = 0x8,
    WIBESOCKET_FRAME_PING = 0x9,
    WIBESOCKET_FRAME_PONG = 0xA
} wibesocket_frame_type_t;

/**
 * @brief WebSocket close codes per RFC 6455
 */
typedef enum {
    WIBESOCKET_CLOSE_NORMAL = 1000,
    WIBESOCKET_CLOSE_GOING_AWAY = 1001,
    WIBESOCKET_CLOSE_PROTOCOL_ERROR = 1002,
    WIBESOCKET_CLOSE_UNSUPPORTED_DATA = 1003,
    WIBESOCKET_CLOSE_NO_STATUS = 1005,
    WIBESOCKET_CLOSE_ABNORMAL = 1006,
    WIBESOCKET_CLOSE_INVALID_PAYLOAD = 1007,
    WIBESOCKET_CLOSE_POLICY_VIOLATION = 1008,
    WIBESOCKET_CLOSE_TOO_LARGE = 1009,
    WIBESOCKET_CLOSE_INTERNAL_ERROR = 1011
} wibesocket_close_code_t;

/**
 * @brief WebSocket configuration options
 */
typedef struct {
    const char* user_agent;         ///< User-Agent header value (optional)
    const char* origin;             ///< Origin header value (optional)
    const char* protocol;           ///< Subprotocol to negotiate (optional)
    uint32_t handshake_timeout_ms;  ///< Handshake timeout in milliseconds
    uint32_t max_frame_size;        ///< Maximum frame size in bytes
    bool enable_compression;        ///< Enable per-message deflate (if supported)
} wibesocket_config_t;

/**
 * @brief WebSocket message structure for receiving data
 */
typedef struct {
    wibesocket_frame_type_t type;   ///< Frame type (text, binary, etc.)
    const void* payload;            ///< Message payload (zero-copy when possible)
    size_t payload_len;             ///< Payload length in bytes
    bool is_final;                  ///< True if this is the final frame in a message
} wibesocket_message_t;

/**
 * @brief Error codes returned by WebSocket functions
 */
typedef enum {
    WIBESOCKET_OK = 0,              ///< Operation completed successfully
    WIBESOCKET_ERROR_INVALID_ARGS,  ///< Invalid arguments provided
    WIBESOCKET_ERROR_MEMORY,        ///< Memory allocation failed
    WIBESOCKET_ERROR_NETWORK,       ///< Network operation failed
    WIBESOCKET_ERROR_HANDSHAKE,     ///< WebSocket handshake failed
    WIBESOCKET_ERROR_PROTOCOL,      ///< Protocol violation
    WIBESOCKET_ERROR_TIMEOUT,       ///< Operation timed out
    WIBESOCKET_ERROR_CLOSED,        ///< Connection is closed
    WIBESOCKET_ERROR_BUFFER_FULL,   ///< Receive buffer is full
    WIBESOCKET_ERROR_NOT_READY      ///< Connection not ready for operation
} wibesocket_error_t;

/**
 * @brief Create a new WebSocket connection
 * 
 * Establishes a WebSocket connection to the specified URI. The URI should
 * be in the format "ws://host:port/path" or "wss://host:port/path" (for
 * secure connections, TLS termination is expected upstream).
 * 
 * @param uri WebSocket URI to connect to
 * @param config Optional configuration parameters (NULL for defaults)
 * @return WebSocket connection handle on success, NULL on failure
 * 
 * @note The returned handle must be freed with wibesocket_close()
 * @note This function is non-blocking and returns immediately
 */
wibesocket_conn_t* wibesocket_connect(const char* uri, const wibesocket_config_t* config);

/**
 * @brief Send a text message over the WebSocket connection
 * 
 * Sends a UTF-8 encoded text message. The message is automatically
 * fragmented if it exceeds the maximum frame size.
 * 
 * @param conn WebSocket connection handle
 * @param text UTF-8 encoded text message
 * @param len Length of the text message in bytes
 * @return WIBESOCKET_OK on success, error code on failure
 * 
 * @note This function may block if the send buffer is full
 */
wibesocket_error_t wibesocket_send_text(wibesocket_conn_t* conn, const char* text, size_t len);

/**
 * @brief Send a binary message over the WebSocket connection
 * 
 * Sends binary data. The data is automatically fragmented if it exceeds
 * the maximum frame size.
 * 
 * @param conn WebSocket connection handle
 * @param data Binary data to send
 * @param len Length of the data in bytes
 * @return WIBESOCKET_OK on success, error code on failure
 * 
 * @note This function may block if the send buffer is full
 */
wibesocket_error_t wibesocket_send_binary(wibesocket_conn_t* conn, const void* data, size_t len);

/**
 * @brief Send a ping frame
 * 
 * Sends a ping frame with optional application data (max 125 bytes).
 * 
 * @param conn WebSocket connection handle
 * @param data Optional ping data (NULL for no data)
 * @param len Length of ping data (0-125 bytes)
 * @return WIBESOCKET_OK on success, error code on failure
 */
wibesocket_error_t wibesocket_send_ping(wibesocket_conn_t* conn, const void* data, size_t len);

/**
 * @brief Send a close frame
 * 
 * Initiates a graceful connection close with the specified close code
 * and reason.
 * 
 * @param conn WebSocket connection handle
 * @param code Close code (see wibesocket_close_code_t)
 * @param reason Optional close reason (NULL for no reason)
 * @return WIBESOCKET_OK on success, error code on failure
 */
wibesocket_error_t wibesocket_send_close(wibesocket_conn_t* conn, uint16_t code, const char* reason);

/**
 * @brief Receive a message from the WebSocket connection
 * 
 * Receives the next available message. The returned message structure
 * contains a pointer to the payload data and metadata about the message.
 * 
 * @param conn WebSocket connection handle
 * @param msg Pointer to message structure to fill
 * @param timeout_ms Timeout in milliseconds (0 for non-blocking, -1 for infinite)
 * @return WIBESOCKET_OK on success, error code on failure
 * 
 * @note The payload pointer is valid until the next call to wibesocket_recv()
 * @note For fragmented messages, this function returns each frame separately
 */
wibesocket_error_t wibesocket_recv(wibesocket_conn_t* conn, wibesocket_message_t* msg, int timeout_ms);

/**
 * @brief Get the current connection state
 * 
 * @param conn WebSocket connection handle
 * @return Current connection state
 */
wibesocket_state_t wibesocket_get_state(const wibesocket_conn_t* conn);

/**
 * @brief Get the last error that occurred on the connection
 * 
 * @param conn WebSocket connection handle
 * @return Last error code, or WIBESOCKET_OK if no error
 */
wibesocket_error_t wibesocket_get_error(const wibesocket_conn_t* conn);

/**
 * @brief Close the WebSocket connection and free resources
 * 
 * Initiates a graceful close if the connection is still open, then
 * frees all associated resources. The connection handle becomes invalid
 * after this call.
 * 
 * @param conn WebSocket connection handle to close and free
 * @return WIBESOCKET_OK on success, error code on failure
 * 
 * @note This function should be called exactly once per connection handle
 */
wibesocket_error_t wibesocket_close(wibesocket_conn_t* conn);

/**
 * @brief Get a human-readable error message
 * 
 * @param error Error code
 * @return Pointer to static string describing the error
 */
const char* wibesocket_error_string(wibesocket_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* WIBESOCKET_H */
