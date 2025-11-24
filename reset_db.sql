DROP DATABASE IF EXISTS kv_store;
CREATE DATABASE kv_store;
USE kv_store;
CREATE TABLE kv_pairs (
    id VARCHAR(255) PRIMARY KEY,
    value TEXT
);