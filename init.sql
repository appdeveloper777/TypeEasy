-- Crear base de datos con UTF-8
CREATE DATABASE IF NOT EXISTS test_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE test_db;

-- Crear tabla usuarios con UTF-8
CREATE TABLE IF NOT EXISTS usuarios (
    id INT AUTO_INCREMENT PRIMARY KEY,
    nombre VARCHAR(100),
    email VARCHAR(100),
    edad INT
) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- Insertar datos de prueba con acentos
INSERT INTO usuarios (nombre, email, edad) VALUES
('Juan Pérez', 'juan@example.com', 30),
('María García', 'maria@example.com', 25),
('Carlos López', 'carlos@example.com', 35);
