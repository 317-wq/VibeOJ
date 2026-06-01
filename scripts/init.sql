-- VibeOJ Database Initialization
-- Executed automatically by MySQL on first container start

CREATE DATABASE IF NOT EXISTS oj_system;
USE oj_system;

CREATE TABLE IF NOT EXISTS users (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(256) NOT NULL,
    role ENUM('user', 'admin') DEFAULT 'user',
    status ENUM('active', 'disabled') DEFAULT 'active',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS problems (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    title VARCHAR(256) NOT NULL,
    description TEXT NOT NULL,
    difficulty ENUM('easy', 'medium', 'hard') NOT NULL,
    time_limit_ms INT DEFAULT 1000,
    memory_limit_kb INT DEFAULT 262144,
    created_by BIGINT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (created_by) REFERENCES users(id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS test_cases (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    problem_id BIGINT NOT NULL,
    input TEXT NOT NULL,
    expected_output TEXT NOT NULL,
    is_sample BOOLEAN DEFAULT FALSE,
    order_index INT DEFAULT 0,
    FOREIGN KEY (problem_id) REFERENCES problems(id) ON DELETE CASCADE,
    INDEX idx_test_cases_problem_order (problem_id, order_index)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS submissions (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT NOT NULL,
    problem_id BIGINT NOT NULL,
    code MEDIUMTEXT NOT NULL,
    status ENUM('pending', 'compiling', 'running', 'accepted', 'wrong_answer',
                'time_limit', 'memory_limit', 'runtime_error', 'compile_error',
                'system_error')
                DEFAULT 'pending',
    compile_output TEXT,
    passed_cases INT DEFAULT 0,
    total_cases INT DEFAULT 0,
    time_used_ms INT DEFAULT 0,
    memory_used_kb INT DEFAULT 0,
    diff_output TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (problem_id) REFERENCES problems(id) ON DELETE CASCADE,
    INDEX idx_submissions_user (user_id),
    INDEX idx_submissions_problem (problem_id),
    INDEX idx_submissions_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS refresh_tokens (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT NOT NULL,
    token_hash VARCHAR(256) NOT NULL UNIQUE,
    expires_at DATETIME NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX idx_refresh_tokens_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Create application user for both local and remote access
-- Local: mysql_native_password (required by mysql-connector-cpp)
CREATE USER IF NOT EXISTS 'ljt'@'localhost' IDENTIFIED WITH mysql_native_password BY 'lijiatong344A@';
-- Remote: caching_sha2_password
CREATE USER IF NOT EXISTS 'ljt'@'%' IDENTIFIED BY 'lijiatong344A@';
GRANT SELECT, INSERT, UPDATE, DELETE ON oj_system.* TO 'ljt'@'localhost';
GRANT SELECT, INSERT, UPDATE, DELETE ON oj_system.* TO 'ljt'@'%';
FLUSH PRIVILEGES;
