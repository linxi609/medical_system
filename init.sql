
DROP DATABASE IF EXISTS medical_system;
CREATE DATABASE medical_system DEFAULT CHARACTER SET utf8mb4;
USE medical_system;

CREATE TABLE t_admin (
    admin_id VARCHAR(10) PRIMARY KEY,
    name     VARCHAR(50),
    username VARCHAR(50) UNIQUE,
    password VARCHAR(100),
    phone    VARCHAR(20)
);

CREATE TABLE t_doctor (
    doctor_id VARCHAR(10) PRIMARY KEY,
    name      VARCHAR(50) NOT NULL,
    gender    VARCHAR(4),
    title     VARCHAR(20),
    dept      VARCHAR(50),
    phone     VARCHAR(20),
    username  VARCHAR(50) UNIQUE,
    password  VARCHAR(100),
    status    TINYINT DEFAULT 1
);

CREATE TABLE t_patient (
    patient_id  VARCHAR(20) PRIMARY KEY,
    name        VARCHAR(50) NOT NULL,
    gender      VARCHAR(4),
    age         INT,
    id_card     VARCHAR(18) UNIQUE,
    phone       VARCHAR(20),
    address     VARCHAR(200),
    username    VARCHAR(50) UNIQUE,
    password    VARCHAR(100),
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    status      TINYINT DEFAULT 1
);

CREATE TABLE t_medicine (
    med_id       VARCHAR(20) PRIMARY KEY,
    med_name     VARCHAR(50),
    spec         VARCHAR(50),
    unit         VARCHAR(10),
    price        DECIMAL(10,2),
    stock        INT,
    manufacturer VARCHAR(50)
);

CREATE TABLE t_appointment (
    appoint_id   INT PRIMARY KEY AUTO_INCREMENT,
    patient_id   VARCHAR(20) NOT NULL,
    doctor_id    VARCHAR(10) NOT NULL,
    appoint_date DATE,
    time_slot    VARCHAR(10),
    symptom      VARCHAR(200),
    status       TINYINT DEFAULT 0,
    create_time  DATETIME DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_patient (patient_id),
    INDEX idx_doctor_date (doctor_id, appoint_date, status)
);

CREATE TABLE t_visit_record (
    visit_id        INT PRIMARY KEY AUTO_INCREMENT,
    patient_id      VARCHAR(20),
    doctor_id       VARCHAR(10),
    appoint_id      INT,
    visit_time      DATETIME DEFAULT CURRENT_TIMESTAMP,
    chief_complaint TEXT,
    present_illness TEXT,
    temp            DECIMAL(3,1),
    blood_pressure  VARCHAR(10),
    heart_rate      INT
);

CREATE TABLE t_prescription (
    presc_id    INT PRIMARY KEY AUTO_INCREMENT,
    visit_id    INT,
    patient_id  VARCHAR(20),
    doctor_id   VARCHAR(10),
    diagnosis   VARCHAR(200),
    status      TINYINT DEFAULT 1,
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE t_presc_detail (
    detail_id  INT PRIMARY KEY AUTO_INCREMENT,
    presc_id   INT,
    med_id     VARCHAR(20),
    quantity   INT,
    dosage     VARCHAR(100),
    unit_price DECIMAL(10,2),
    subtotal   DECIMAL(10,2)
);

CREATE TABLE t_medical_record (
    record_id       INT PRIMARY KEY AUTO_INCREMENT,
    patient_id      VARCHAR(20),
    doctor_id       VARCHAR(10),
    visit_id        INT,
    allergy_history VARCHAR(200),
    past_history    VARCHAR(200),
    family_history  VARCHAR(200),
    update_time     DATETIME DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO t_admin VALUES ('A001', '系统管理员', 'admin', '123456', '');
INSERT INTO t_doctor VALUES ('D001', '张医生', '男', '主治医师', '内科', '', 'D001', '123456', 1);
INSERT INTO t_medicine VALUES ('M001', '阿莫西林', '0.25g*24粒', '盒', 15.50, 100, '某药厂');
