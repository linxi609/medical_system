# 社区医疗服务中心信息管理系统

C + MySQL 实现的社区医疗信息管理系统，控制台菜单交互，支持管理员、医生、患者三种角色。

> 项目只共享 `init.sql` 建库脚本，不附带数据库文件。克隆后在本机执行一遍脚本，就能得到结构和预置数据一致的库。

---

## 一、环境要求

在 Rocky Linux 上执行以下 5 条检查:

```bash
gcc --version                          # 1. 编译器
sudo systemctl status mysqld           # 2. MySQL 服务
mysql_config --cflags --libs           # 3. MySQL 开发库
git --version                          # 4. 版本控制
mysql -u root -p                       # 5. 能登录 MySQL
```

**5 条全过才算环境 OK。** 有任何一条不过，先解决它，别往下走。

| 报错 | 解决办法 |
|------|----------|
| `mysql_config: command not found` | `sudo dnf install mysql-community-devel` |
| `Access denied` | 密码不对 |
| `Can't connect` | MySQL 没启动 → `sudo systemctl start mysqld` |
| `gcc: command not found` | `sudo dnf install gcc make` |

---

## 二、快速开始

```bash
# 1. 克隆项目
git clone https://github.com/tpzdcc/medical_system.git
cd medical_system

# 2. 建库建表(用你自己的 MySQL 密码)
mysql -u root -p < init.sql

# 3. 验证: 应该看到 9 张表
mysql -u root -p -e "use medical_system; show tables;"

# 4. 编译
make

# 5. 运行(把密码换成你自己的)
DB_PASSWORD='你的MySQL密码' ./bin/medical_system
```

第 5 步如果打印出 `db connect success`，环境就通了。

---

## 三、数据库配置

连接参数**通过环境变量传入，不要把密码写进源码、更不要提交到仓库**:

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `DB_HOST` | `localhost` | 数据库地址 |
| `DB_USER` | `root` | 用户名 |
| `DB_PASSWORD` | (空) | 密码，**未设置时启动会提示** |
| `DB_NAME` | `medical_system` | 库名 |
| `DB_PORT` | `3306` | 端口 |

最简用法只需要给 `DB_PASSWORD`，其余走默认值:

```bash
DB_PASSWORD='你的密码' ./bin/medical_system
```

嫌每次输入麻烦，可以写进 `~/.bashrc`:

```bash
echo "export DB_PASSWORD='你的密码'" >> ~/.bashrc && source ~/.bashrc
```

> ⚠️ 检查一下: 提交前先跑 `git diff --cached`，确认没有把自己的密码带进去。仓库是公开的。

### 表结构

`init.sql` 会创建 9 张表，并预置少量测试数据:

| 表名 | 说明 |
|------|------|
| `t_admin` | 管理员 |
| `t_doctor` | 医生 |
| `t_patient` | 患者 |
| `t_medicine` | 药品 |
| `t_appointment` | 预约挂号 |
| `t_visit_record` | 就诊记录 |
| `t_prescription` | 处方主表 |
| `t_presc_detail` | 处方明细 |
| `t_medical_record` | 病历 |

预置账号:

| 角色 | 登录账号 | 密码 |
|------|----------|------|
| 管理员 | `admin` | `123456` |
| 医生 | `D001`（新增医生时账号 = 医生编号） | `123456` |
| 患者 | 注册成功后发到的患者编号，如 `P20260001` | 注册时本人设的密码 |

**账号由系统生成，不用自己起名**：医生账号就是医生编号（D001、D002…），
患者账号就是患者编号（P20260001…）。编号只增不减、注销/离职后也不回收，
所以不存在“重名注册不进来”的问题，也不会有人借到前一个同名账号的历史记录。
登录界面的“账号”一栏填的就是这个编号。

> 课程项目为简化实现，密码是明文存储的，不要用于任何真实场景。

**改了表结构怎么办?** `init.sql` 是表结构的**唯一权威版本**。任何表结构变更都改这个文件并重新执行；不要只在数据库里手动改表，否则换一台机器重建时必然对不上。

---

## 四、目录结构与分层

```
src/       业务模块、三个角色菜单、菜单公共逻辑
inc/       模块头文件(函数声明和结构体定义)
data/      数据访问层(MySQL 连接、查询、转义、事务)
lib/       公共工具库(通用工具、控制台输入)
obj/       编译中间产物(不入库)
bin/       可执行文件(不入库)
init.sql   建库建表脚本
Makefile
```

调用方向:

```text
src/menu_*.c  →  src/<业务模块>.c  →  data/db_mysql.c  →  MySQL
                        ↓
              lib/utils.c、lib/cli_utils.c
```

模块与文件对应:

| 模块 | 头文件 | 实现文件 |
|------|--------|----------|
| M01 患者管理 | `inc/patient.h` | `src/patient.c` |
| M02 医生管理 | `inc/doctor.h` | `src/doctor.c` |
| M03 挂号预约 | `inc/appointment.h` | `src/appointment.c` |
| M04 就诊记录 | `inc/visit.h` | `src/visit.c` |
| M05 处方管理 | `inc/prescription.h` | `src/prescription.c` |
| M06 药品管理 | `inc/medicine.h` | `src/medicine.c` |
| M07 病历管理 | `inc/medical_record.h` | `src/medical_record.c` |
| M08 登录与用户 | `inc/auth.h` | `src/auth.c` |
| 就诊/病历联查 | `inc/doctor_deps.h` | `src/doctor_deps.c` |
| 三个角色菜单 | `inc/menu.h` | `src/menu_admin.c`、`src/menu_doctor.c`、`src/menu_patient.c` |
| 菜单公共逻辑 | `inc/menu_common.h` | `src/menu_common.c` |

常用命令:

```bash
make          # 编译
make DEBUG=1  # 带调试符号编译(可用 gdb)
make clean    # 清理
```

---

## 五、接口契约

源码不保留注释。公共接口以本节和 `inc/` 下的函数声明为准；模块内部实现直接看对应的 `.c` 文件。

### 公共工具

**数据库访问** `inc/db_mysql.h` / `data/db_mysql.c`

- `db_create` / `db_close`: 建立连接、释放连接
- `db_execute`: 执行 INSERT / UPDATE / DELETE 等语句
- `db_query` / `db_fetch_row` / `db_res_free`: 查询、逐行读取、释放结果集
- `db_escape` / `db_escape_literal`: 转义字符串，后者额外在两侧加单引号
- `db_row_str` / `db_row_int` / `db_row_double`: 按列读取行数据
- `db_begin_tx`: 开启事务；提交和回滚用 `db_execute(db, "COMMIT")` / `db_execute(db, "ROLLBACK")`

**通用工具** `inc/utils.h` / `lib/utils.c`

- `str_trim` / `is_all_digit` / `copy_str`
- `utf8_width` / `utf8_valid`: UTF-8 显示宽度与合法性检查
- `table_row`: 按显示宽度输出表格行

**控制台输入** `inc/cli_utils.h` / `lib/cli_utils.c`

- `cli_read_line`: 读取完整一行，丢弃缓冲区外的剩余字符
- `cli_prompt` / `cli_prompt_int` / `cli_prompt_double`: 带提示的输入
- `cli_read_choice`: 读取菜单选项
- `cli_keep_or`: 直接回车时保留原值

**菜单公共逻辑** `inc/menu_common.h` / `src/menu_common.c`

- `err_text`: 返回码转提示文案
- `appt_status_text` / `presc_status_text`: 状态转显示文案
- `menu_change_password`: 三个角色共用的修改密码流程

### 数字约定

项目不使用常量宏，判断时直接写字面值。含义如下:

| 用途 | 取值 |
|------|------|
| 角色 | `1` 管理员，`2` 医生，`3` 患者 |
| 预约状态 | `0` 待就诊，`1` 已就诊，`2` 已取消 |
| 处方状态 | `1` 有效，`0` 已作废 |
| 库存预警 | 库存低于 `50` 视为不足 |

### 返回值约定

业务函数成功返回 `0`，失败返回负值。调用方统一用 `err_text` 转成提示文案。

| 返回值 | 含义 |
|--------|------|
| `0` | 成功 |
| `-1` | 数据库连接失败；少数内部函数也用于结果读取失败 |
| `-2` | SQL 执行失败；参数为空、字段超长或转义失败也返回它 |
| `-3` | 唯一字段重复(身份证号/账号/编号等)；导出函数也用于路径无效或写文件失败 |
| `-4` | 记录不存在，或传入的编号无效 |
| `-5` | 库存不足 |
| `-6` | 越权操作、状态不允许或角色非法；个别函数也用于空参数 |
| `-7` | 账号锁定、账号停用或病历超时锁定 |
| `-8` | 重复挂号 |

> `-2`、`-3` 在不同函数中有复用；某个函数具体会返回哪些值，以该函数的实现为准。

### 输入约定

- 控制台输入统一走 `cli_utils.c`。`fgets` 读满缓冲区时读不到换行，剩余字符会被下一个问题读走，导致后续字段全部错位。`cli_read_line` 会读完整行并丢弃多余字符。
- C 的 `char[N]` 按**字节**计算，一个汉字在 UTF-8 中占 3 字节；MySQL 的 `VARCHAR(N)` 按**字符**计算。字段宽度以 `inc/*.h` 中的结构体定义为准，不要直接照抄列宽。
- 输入内容会在提问处做 UTF-8 校验，非法编码会要求重新输入。

---

## 六、常见问题

**编译报 `mysql/mysql.h: No such file or directory`**
缺开发库:`sudo dnf install mysql-community-devel`

**启动只打印 "未设置 DB_PASSWORD 环境变量" 然后连接失败**
没传密码，用 `DB_PASSWORD='你的密码' ./bin/medical_system` 运行。

**`db connect error: Access denied`**
密码错了，或库没建。先跑 `mysql -u root -p -e "show databases;"` 确认 `medical_system` 存在。

**改了 `init.sql` 之后表还是旧的**
脚本开头是 `DROP DATABASE IF EXISTS`，重跑会**清空重建**。确认没有你要保留的数据后再执行。

**`make` 报 `mysql_config: not found`**
Makefile 依赖 `mysql_config`，没装开发库就会这样，见第一条。

**导出患者列表提示"写不进去"**
默认导出到 `data/patients_export.txt`。程序一般在 `bin/` 下运行，那里没有 `data/`
目录，程序会先建目录，建不出来就退回到当前目录用同名文件，并在提示里打印
**文件的实际绝对路径**。按那个路径去找文件即可。

**挂号/录入中文时报 `Incorrect string value: '\xE5\xB1\xE6...' for column ...`**
MySQL 拒收的不是 UTF-8 的字节。这个库和连接都是 utf8mb4，所以问题出在**输入**上:
终端或粘贴内容的编码不是 UTF-8(比如从 GBK 的 Windows 终端复制过来的)，
或者这一行太长、被输入缓冲区截断在汉字中间。程序现在会在提问处直接拦下来重问，
不会再把这类字节发给数据库。确认终端用 UTF-8:`locale` 的输出里应有 `UTF-8`。
