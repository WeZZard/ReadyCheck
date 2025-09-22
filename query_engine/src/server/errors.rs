use std::io;

use serde_json::Value;
use thiserror::Error;

use super::types::JsonRpcError;

#[derive(Debug, Error)]
pub enum ServerError {
    #[error("hyper server error: {0}")]
    Hyper(#[from] hyper::Error),
    #[error("failed to bind address: {0}")]
    Io(#[from] io::Error),
}

#[derive(Debug, Error, Clone)]
pub enum JsonRpcServerError {
    #[error("connection limit reached")]
    ConnectionLimit,
    #[error("rate limit exceeded")]
    RateLimited,
    #[error("method not found: {0}")]
    MethodNotFound(String),
    #[error("invalid request: {0}")]
    InvalidRequest(String),
    #[error("internal error: {0}")]
    Internal(String),
}

impl From<JsonRpcServerError> for JsonRpcError {
    fn from(value: JsonRpcServerError) -> Self {
        match value {
            JsonRpcServerError::ConnectionLimit => JsonRpcError::too_many_connections(),
            JsonRpcServerError::RateLimited => JsonRpcError::rate_limited(),
            JsonRpcServerError::MethodNotFound(method) => JsonRpcError::method_not_found(&method),
            JsonRpcServerError::InvalidRequest(msg) => JsonRpcError::invalid_request(msg),
            JsonRpcServerError::Internal(msg) => JsonRpcError::internal(msg),
        }
    }
}

impl JsonRpcServerError {
    pub fn to_response(&self, id: Option<Value>) -> super::types::JsonRpcResponse {
        super::types::JsonRpcResponse::error(id, self.clone().into())
    }
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;
    use serde_json::json;

    #[test]
    fn json_rpc_errors__server_error_conversion__then_matches_codes() {
        let connection = JsonRpcServerError::ConnectionLimit;
        let as_json: JsonRpcError = connection.clone().into();
        assert_eq!(as_json.code, -32002);
        assert_eq!(as_json.message, "Too many concurrent connections");
        assert!(as_json.data.is_none());

        let rate = JsonRpcServerError::RateLimited;
        let as_json: JsonRpcError = rate.clone().into();
        assert_eq!(as_json.code, -32001);
        assert_eq!(as_json.message, "Too many requests");
        assert!(as_json.data.is_none());

        let method = JsonRpcServerError::MethodNotFound("trace.echo".into());
        let as_json: JsonRpcError = method.clone().into();
        assert_eq!(as_json.code, -32601);
        assert_eq!(as_json.message, "Method not found");
        assert_eq!(as_json.data, Some(json!("trace.echo")));

        let invalid = JsonRpcServerError::InvalidRequest("missing jsonrpc".into());
        let as_json: JsonRpcError = invalid.clone().into();
        assert_eq!(as_json.code, -32600);
        assert_eq!(as_json.message, "Invalid request");
        assert_eq!(as_json.data, Some(json!("missing jsonrpc")));

        let internal = JsonRpcServerError::Internal("panic".into());
        let as_json: JsonRpcError = internal.clone().into();
        assert_eq!(as_json.code, -32603);
        assert_eq!(as_json.message, "Internal error");
        assert_eq!(as_json.data, Some(json!("panic")));
    }

    #[test]
    fn json_rpc_errors__to_response__then_builds_json_rpc_payload() {
        let err = JsonRpcServerError::MethodNotFound("trace.slow".into());
        let response = err.to_response(Some(json!(99)));

        assert_eq!(response.jsonrpc, "2.0");
        assert_eq!(response.id, Some(json!(99)));
        assert!(response.result.is_none());
        let inner = response.error.expect("expected error");
        assert_eq!(inner.code, -32601);
        assert_eq!(inner.message, "Method not found");
    }

    #[test]
    fn json_rpc_errors__server_error_display__then_contains_context() {
        let err = JsonRpcServerError::InvalidRequest("bad".into());
        let rendered = err.to_string();
        assert!(rendered.contains("invalid request"));
        assert!(rendered.contains("bad"));
    }
}
