use std::{
    net::IpAddr,
    sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
};

use dashmap::DashMap;
use thiserror::Error;

#[derive(Debug, Clone, Copy)]
pub struct ConnectionManagerConfig {
    pub max_total: usize,
    pub max_per_ip: usize,
}

impl Default for ConnectionManagerConfig {
    fn default() -> Self {
        Self {
            max_total: 10_000,
            max_per_ip: 1_000,
        }
    }
}

#[derive(Debug, Error)]
pub enum ConnectionError {
    #[error("global connection limit reached")]
    GlobalLimit,
    #[error("per-ip connection limit reached for {0}")]
    PerIpLimit(IpAddr),
}

#[derive(Debug)]
struct ConnectionManagerInner {
    config: ConnectionManagerConfig,
    total: AtomicUsize,
    per_ip: DashMap<IpAddr, usize>,
}

impl ConnectionManagerInner {
    fn new(config: ConnectionManagerConfig) -> Self {
        Self {
            config,
            total: AtomicUsize::new(0),
            per_ip: DashMap::new(),
        }
    }
}

#[derive(Clone, Debug)]
pub struct ConnectionManager {
    inner: Arc<ConnectionManagerInner>,
}

impl ConnectionManager {
    pub fn new(config: ConnectionManagerConfig) -> Self {
        Self {
            inner: Arc::new(ConnectionManagerInner::new(config)),
        }
    }

    pub fn acquire(&self, ip: IpAddr) -> Result<ConnectionGuard, ConnectionError> {
        let total = self.inner.total.fetch_add(1, Ordering::AcqRel) + 1;
        if self.inner.config.max_total != 0 && total > self.inner.config.max_total {
            self.inner.total.fetch_sub(1, Ordering::AcqRel);
            return Err(ConnectionError::GlobalLimit);
        }

        let mut entry = self.inner.per_ip.entry(ip).or_insert(0);
        *entry += 1;
        if self.inner.config.max_per_ip != 0 && *entry > self.inner.config.max_per_ip {
            *entry -= 1;
            self.inner.total.fetch_sub(1, Ordering::AcqRel);
            return Err(ConnectionError::PerIpLimit(ip));
        }

        Ok(ConnectionGuard {
            manager: self.clone(),
            ip,
            released: false,
        })
    }

    pub fn release(&self, ip: IpAddr) {
        self.inner.total.fetch_sub(1, Ordering::AcqRel);
        if let Some(mut entry) = self.inner.per_ip.get_mut(&ip) {
            if *entry > 1 {
                *entry -= 1;
            } else {
                drop(entry);
                self.inner.per_ip.remove(&ip);
            }
        }
    }

    pub fn active_total(&self) -> usize {
        self.inner.total.load(Ordering::Acquire)
    }

    pub fn active_for_ip(&self, ip: IpAddr) -> usize {
        self.inner.per_ip.get(&ip).map(|entry| *entry).unwrap_or(0)
    }
}

pub struct ConnectionGuard {
    manager: ConnectionManager,
    ip: IpAddr,
    released: bool,
}

impl ConnectionGuard {
    pub fn ip(&self) -> IpAddr {
        self.ip
    }

    pub fn release(mut self) {
        if !self.released {
            self.manager.release(self.ip);
            self.released = true;
        }
    }
}

impl Drop for ConnectionGuard {
    fn drop(&mut self) {
        if !self.released {
            self.manager.release(self.ip);
            self.released = true;
        }
    }
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;
    use std::net::{IpAddr, Ipv4Addr};

    fn localhost() -> IpAddr {
        IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))
    }

    #[test]
    fn json_rpc_connection__multiple_per_ip_release__then_decrements_correctly() {
        // Test lines 89-90: releasing when count > 1
        let manager = ConnectionManager::new(ConnectionManagerConfig {
            max_total: 10,
            max_per_ip: 3,
        });
        let ip = localhost();

        // Acquire 3 connections from same IP
        let guard1 = manager.acquire(ip).expect("first");
        let guard2 = manager.acquire(ip).expect("second");
        let guard3 = manager.acquire(ip).expect("third");
        assert_eq!(manager.active_for_ip(ip), 3);

        // Release first - should decrement to 2 (line 90)
        drop(guard1);
        assert_eq!(manager.active_for_ip(ip), 2);

        // Release second - should decrement to 1 (line 90)
        drop(guard2);
        assert_eq!(manager.active_for_ip(ip), 1);

        // Release third - should remove entry (lines 92-93)
        drop(guard3);
        assert_eq!(manager.active_for_ip(ip), 0);
    }

    #[test]
    fn json_rpc_connection__config_default__then_provides_sensible_defaults() {
        let config = ConnectionManagerConfig::default();
        assert_eq!(config.max_total, 10_000);
        assert_eq!(config.max_per_ip, 1_000);
    }

    #[test]
    fn json_rpc_connection__acquire_and_drop__then_counters_reset() {
        let manager = ConnectionManager::new(ConnectionManagerConfig {
            max_total: 10,
            max_per_ip: 5,
        });
        let ip = localhost();

        {
            let guard = manager.acquire(ip).expect("expected acquisition");
            assert_eq!(guard.ip(), ip);
            assert_eq!(manager.active_total(), 1);
            assert_eq!(manager.active_for_ip(ip), 1);
        }

        assert_eq!(manager.active_total(), 0);
        assert_eq!(manager.active_for_ip(ip), 0);
    }

    #[test]
    fn json_rpc_connection__manual_release__then_drop_does_not_double_release() {
        let manager = ConnectionManager::new(ConnectionManagerConfig {
            max_total: 10,
            max_per_ip: 5,
        });
        let ip = localhost();

        let guard = manager.acquire(ip).expect("expected acquisition");
        assert_eq!(manager.active_total(), 1);

        guard.release();
        assert_eq!(manager.active_total(), 0);
        assert_eq!(manager.active_for_ip(ip), 0);
    }

    #[test]
    fn json_rpc_connection__exceed_global_limit__then_errors() {
        let manager = ConnectionManager::new(ConnectionManagerConfig {
            max_total: 1,
            max_per_ip: 5,
        });
        let ip1 = localhost();
        let ip2 = IpAddr::V4(Ipv4Addr::new(192, 168, 0, 2));

        let _guard = manager.acquire(ip1).expect("first connection works");
        let err = match manager.acquire(ip2) {
            Ok(_) => panic!("expected global limit error"),
            Err(err) => err,
        };
        assert!(matches!(err, ConnectionError::GlobalLimit));
        assert_eq!(manager.active_total(), 1);
    }

    #[test]
    fn json_rpc_connection__exceed_per_ip_limit__then_errors() {
        let manager = ConnectionManager::new(ConnectionManagerConfig {
            max_total: 10,
            max_per_ip: 1,
        });
        let ip = localhost();

        let _guard = manager.acquire(ip).expect("first connection works");
        let err = match manager.acquire(ip) {
            Ok(_) => panic!("expected per ip limit"),
            Err(err) => err,
        };
        match err {
            ConnectionError::PerIpLimit(actual) => assert_eq!(actual, ip),
            _ => panic!("unexpected error: {err:?}"),
        }
        assert_eq!(manager.active_total(), 1);
        assert_eq!(manager.active_for_ip(ip), 1);
    }
}
