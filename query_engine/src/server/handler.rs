use std::{future::Future, sync::Arc};

use async_trait::async_trait;
use dashmap::DashMap;
use serde_json::Value;

use super::types::JsonRpcError;

pub type JsonRpcResult = Result<Value, JsonRpcError>;

#[async_trait]
pub trait JsonRpcHandler: Send + Sync {
    async fn call(&self, params: Option<Value>) -> JsonRpcResult;
}

struct FnHandler<F>
where
    F: Send + Sync,
{
    func: Arc<F>,
}

#[async_trait]
impl<F, Fut> JsonRpcHandler for FnHandler<F>
where
    F: Fn(Option<Value>) -> Fut + Send + Sync,
    Fut: Future<Output = JsonRpcResult> + Send,
{
    async fn call(&self, params: Option<Value>) -> JsonRpcResult {
        (self.func)(params).await
    }
}

#[derive(Clone, Default)]
pub struct HandlerRegistry {
    handlers: Arc<DashMap<String, Arc<dyn JsonRpcHandler>>>,
}

impl HandlerRegistry {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn register_handler<H>(&self, method: impl Into<String>, handler: H)
    where
        H: JsonRpcHandler + 'static,
    {
        self.handlers
            .insert(method.into(), Arc::new(handler) as Arc<dyn JsonRpcHandler>);
    }

    pub fn register_async<F, Fut>(&self, method: impl Into<String>, func: F)
    where
        F: Fn(Option<Value>) -> Fut + Send + Sync + 'static,
        Fut: Future<Output = JsonRpcResult> + Send + 'static,
    {
        let handler = FnHandler {
            func: Arc::new(func),
        };
        self.register_handler(method, handler);
    }

    pub fn register_sync<F>(&self, method: impl Into<String>, func: F)
    where
        F: Fn(Option<Value>) -> JsonRpcResult + Send + Sync + 'static,
    {
        let func = Arc::new(func);
        self.register_async(method, move |params| {
            let func = Arc::clone(&func);
            async move { (*func)(params) }
        });
    }

    pub async fn call(&self, method: &str, params: Option<Value>) -> JsonRpcResult {
        match self.handlers.get(method) {
            Some(handler) => handler.call(params).await,
            None => Err(JsonRpcError::method_not_found(method)),
        }
    }

    pub fn contains(&self, method: &str) -> bool {
        self.handlers.contains_key(method)
    }
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;
    use serde_json::json;
    use std::sync::Arc;

    #[tokio::test]
    async fn json_rpc_handler__async_registration__then_invokes_handler() {
        let registry = HandlerRegistry::new();
        registry.register_async("trace.echo", |params| async move {
            Ok(params.unwrap_or_else(|| json!({"default": true})))
        });

        let result = registry
            .call("trace.echo", Some(json!({"value": 7})))
            .await
            .expect("handler should succeed");

        assert_eq!(result, json!({"value": 7}));
    }

    #[tokio::test]
    async fn json_rpc_handler__sync_registration__then_invokes_handler() {
        let registry = HandlerRegistry::new();
        let flag = Arc::new(std::sync::atomic::AtomicUsize::new(0));
        let flag_clone = Arc::clone(&flag);
        registry.register_sync("trace.count", move |_| {
            flag_clone.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            Ok(json!({"count": 1}))
        });

        let result = registry
            .call("trace.count", None)
            .await
            .expect("handler should succeed");

        assert_eq!(result, json!({"count": 1}));
        assert_eq!(flag.load(std::sync::atomic::Ordering::SeqCst), 1);
    }

    #[tokio::test]
    async fn json_rpc_handler__unknown_method__then_method_not_found_error() {
        let registry = HandlerRegistry::new();
        let err = registry
            .call("trace.missing", None)
            .await
            .expect_err("expected method not found error");

        assert_eq!(err.code, -32601);
        assert_eq!(err.message, "Method not found");
        assert_eq!(err.data, Some(json!("trace.missing")));
    }

    #[tokio::test]
    async fn json_rpc_handler__handler_error__then_propagates() {
        let registry = HandlerRegistry::new();
        registry.register_async("trace.fail", |_params| async {
            Err(JsonRpcError::invalid_params("bad"))
        });

        let err = registry
            .call("trace.fail", None)
            .await
            .expect_err("expected handler error");

        assert_eq!(err.code, -32602);
        assert_eq!(err.message, "Invalid params");
    }

    #[test]
    fn json_rpc_handler__contains__then_tracks_registration() {
        let registry = HandlerRegistry::new();
        assert!(!registry.contains("trace.echo"));
        registry.register_sync("trace.echo", |_| Ok(json!(null)));
        assert!(registry.contains("trace.echo"));
    }
}
