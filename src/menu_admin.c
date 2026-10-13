/* ==========================================================================
 * menu_admin.c —— 管理员主菜单 实现
 * --------------------------------------------------------------------------
 * 负责组：管理员组
 * 对应头文件：inc/menu.h
 * 对应界面原型：教程 1.7 "管理员主菜单"
 *
 * 菜单项和各自调用的函数（与 menu.h 里 menu_admin 的注释一致）：
 *   1. 患者档案管理  -> patient_query_all / patient_query_by_id /
 *                       patient_update_self / patient_delete / patient_export
 *   2. 医生管理      -> doctor_query_all / doctor_query_by_id /
 *                       doctor_add / doctor_update / doctor_delete
 *   3. 预约查询      -> appointment_query_all
 *   4. 就诊记录查询  -> visit_query_all
 *   5. 处方查询      -> prescription_query_all
 *   6. 药品管理      -> medicine_query_all / medicine_query_by_id /
 *                       medicine_add / medicine_stock_in / medicine_stock_out /
 *                       medicine_update_price / medicine_delete
 *   7. 病历查询      -> medical_record_query_all
 *   8. 修改密码      -> user_change_password
 *   0. 退出系统
 *
 * 说明：
 *   - 输入一律按 main.c 里 read_line 的写法读整行，不用 scanf("%d")，
 *     避免 scanf 遇到字母卡住、以及残留字符窜到下一次输入里。
 *   - 所有失败分支都按各模块注释里约定的返回码给出中文提示。
 *   - 查询类（3/4/5/7）依赖其它组的模块，那些文件目前还是空壳时，
 *     这里照常调用，返回 0 条就提示"暂无记录"，不影响本菜单编译运行。
 * ========================================================================== */

#include "menu.h"
#include "patient.h"
#include "doctor.h"
#include "medicine.h"
#include "appointment.h"
#include "visit.h"
#include "prescription.h"
#include "medical_record.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ==========================================================================
 * 输入辅助（写法参照 src/main.c 里的 read_line / prompt）
 * ========================================================================== */

/* 读一整行，去掉末尾换行。返回 0 成功，-1 读失败/输入结束
 *
 * 注意 else 分支不能省：fgets 最多只读 size-1 字节，若这一行比缓冲区还长，
 * 它会刚好填满缓冲区、连换行都没读到就返回，剩下的字符会被下一次读取
 * 当成新的一行，把后面每个字段都顶错一位且不报错。 */
static int read_line(char* buf, int size) {
    if (!fgets(buf, size, stdin)) return -1;

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';                /* 正常情况：读到换行，去掉它 */
    } else {
        int c;                              /* 缓冲区装满了，丢掉本行剩下的字符 */
        while ((c = getchar()) != '\n' && c != EOF) { /* 空循环 */ }
    }
    return 0;
}

/* 显示提示语后读一整行字符串。
 * 读到不是合法 UTF-8 的字节就重问：这种输入原样发给 MySQL 会被拒
 * （Incorrect string value），在这里拦下来才能告诉用户真正的原因。 */
static void prompt(const char* label, char* buf, int size) {
    for (;;) {
        printf("%s", label);
        fflush(stdout);
        if (read_line(buf, size) != 0) { buf[0] = '\0'; return; }
        if (utf8_valid(buf)) return;
        printf("输入里含有非 UTF-8 的字节，MySQL 会拒绝这样的内容。常见原因有两个：\n"
               "  1) 当前终端/粘贴内容的编码不是 UTF-8 —— 确认 locale 里带 UTF-8；\n"
               "  2) 这一行太长，被输入缓冲区截断，末尾正好切在汉字中间 —— 请分短一些。\n"
               "请修改后重新输入：\n");
    }
}

/* 显示提示语后读一个整数 */
static void prompt_int(const char* label, int* out) {
    char buf[32];
    prompt(label, buf, sizeof(buf));
    *out = atoi(buf);
}

/* 显示提示语后读一个浮点数 */
static void prompt_double(const char* label, double* out) {
    char buf[32];
    prompt(label, buf, sizeof(buf));
    *out = atof(buf);
}

/* 读一个菜单选项。返回 0 成功，-1 输入非法，-2 输入结束（Ctrl+D 或管道读完） */
static int read_choice(int* out) {
    char buf[32];
    if (read_line(buf, sizeof(buf)) != 0) return -2;
    if (sscanf(buf, "%d", out) != 1) return -1;
    return 0;
}

/* 输入为空时用旧值兜底（修改信息里"直接回车保留原值"） */
static void keep_or(const char* defval, char* buf, int size) {
    if (buf[0] == '\0' && defval && defval[0]) {
        snprintf(buf, (size_t)size, "%s", defval);
    }
}

/* 返回码 -> 中文原因，供各失败分支打印 */
static const char* err_text(int ret) {
    switch (ret) {
        case -1: return "数据库连接失败";
        case -2: return "SQL 执行失败";
        case -3: return "唯一字段重复";
        case -4: return "记录不存在";
        case -5: return "库存不足";
        case -6: return "越权操作或状态不允许";
        case -7: return "账号或病历已锁定";
        case -8: return "重复挂号";
        default: return "未知错误";
    }
}

/* 预约状态：t_appointment.status 0 待就诊 / 1 已就诊 / 2 已取消 */
static const char* appt_status_text(int status) {
    switch (status) {
        case 0: return "待就诊";
        case 1: return "已就诊";
        case 2: return "已取消";
        default: return "未知";
    }
}

/* 处方状态：t_prescription.status 1 有效 / 0 作废 */
static const char* presc_status_text(int status) {
    return status == 1 ? "有效" : "已作废";
}

/* ==========================================================================
 * 1. 患者档案管理（查 / 改 / 注销 / 导出）
 * ========================================================================== */

/* 查询全部患者 */
static void patient_list(DB_HANDLE* db) {
    Patient* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = patient_query_all(db, &list, &count);
    if (ret != 0) {
        printf("[查询患者失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 患者档案（共 %d 人）===\n", count);
    if (count == 0) {
        printf("  暂无患者档案。\n");
        patient_list_free(list);
        return;
    }

    {
        /* 列宽按【显示列】算：汉字占 2 列。负数 = 右对齐 */
        const int   w[] = {12, 12, 4, -4, 20, 14, 0};
        const char* cells[7];
        char        age_txt[8];

        cells[0] = "患者编号"; cells[1] = "姓名";  cells[2] = "性别";
        cells[3] = "年龄";     cells[4] = "身份证号"; cells[5] = "电话";
        cells[6] = "状态";
        table_row(cells, w, 7);

        for (i = 0; i < count; i++) {
            snprintf(age_txt, sizeof(age_txt), "%d", list[i].age);
            cells[0] = list[i].patient_id;
            cells[1] = list[i].name;
            cells[2] = list[i].gender;
            cells[3] = age_txt;
            cells[4] = list[i].id_card;
            cells[5] = list[i].phone;
            cells[6] = list[i].status == 1 ? "正常" : "已注销";
            table_row(cells, w, 7);
        }
    }

    patient_list_free(list);
}

/* 按编号查询单个患者 */
static void patient_query(DB_HANDLE* db) {
    char patient_id[20];
    Patient p;
    int ret;

    prompt("请输入患者编号（如 P20260001，0 返回）：", patient_id, sizeof(patient_id));
    if (patient_id[0] == '\0' || strcmp(patient_id, "0") == 0) return;

    memset(&p, 0, sizeof(p));
    ret = patient_query_by_id(db, patient_id, &p);
    if (ret == -4) {
        printf("没有编号为 %s 的患者，或该患者已注销。\n", patient_id);
        return;
    } else if (ret != 0) {
        printf("查询失败：%s\n", err_text(ret));
        return;
    }

    printf("\n--- 患者信息 ---\n");
    printf("  患者编号：%s\n", p.patient_id);
    printf("  姓名：    %s\n", p.name);
    printf("  性别：    %s\n", p.gender);
    printf("  年龄：    %d\n", p.age);
    printf("  身份证号：%s\n", p.id_card);
    printf("  电话：    %s\n", p.phone);
    printf("  住址：    %s\n", p.address[0] ? p.address : "（未填）");
    printf("  注册时间：%s\n", p.create_time[0] ? p.create_time : "未知");
    printf("  状态：    %s\n", p.status == 1 ? "正常" : "已注销");
}

/* 修改患者联系方式（电话/住址） */
static void patient_update(DB_HANDLE* db) {
    char patient_id[20];
    char phone[20];
    char address[200];
    int ret;

    prompt("请输入患者编号（0 返回）：", patient_id, sizeof(patient_id));
    if (patient_id[0] == '\0' || strcmp(patient_id, "0") == 0) return;

    prompt("新电话：", phone, sizeof(phone));
    prompt("新住址：", address, sizeof(address));

    ret = patient_update_self(db, patient_id, phone, address);
    if (ret == 0) {
        printf("修改成功。\n");
    } else if (ret == -4) {
        printf("修改失败：患者不存在或已注销\n");
    } else {
        printf("修改失败：%s\n", err_text(ret));
    }
}

/* 注销患者（软删除） */
static void patient_delete_flow(DB_HANDLE* db) {
    char patient_id[20];
    char ans[16];
    int ret;

    prompt("请输入要注销的患者编号（0 返回）：", patient_id, sizeof(patient_id));
    if (patient_id[0] == '\0' || strcmp(patient_id, "0") == 0) return;

    prompt("确认注销该患者？注销后不能登录、不能挂号。(y/N)：", ans, sizeof(ans));
    if (ans[0] != 'y' && ans[0] != 'Y') {
        printf("已取消。\n");
        return;
    }

    ret = patient_delete(db, patient_id);
    if (ret == 0) {
        printf("患者 %s 已注销。\n", patient_id);
    } else if (ret == -4) {
        printf("注销失败：患者不存在或已注销\n");
    } else {
        printf("注销失败：%s\n", err_text(ret));
    }
}

/* 准备导出路径：目录不存在就建出来，建不出来或写不进去就退回当前目录的同名文件。
 * 成功时把最终要用的路径写进 actual 并返回 0；两个方案都不可写返回 -1。
 *
 * 为什么需要这一步：程序通常在 bin/ 目录下运行，那里没有 data/ 子目录，
 * 直接 fopen("data/patients_export.txt") 会失败 —— 而失败原因只有一句
 * "SQL 执行失败"，看不出是目录不存在。 */
static int prepare_out_path(const char* path, char* actual, size_t actualsz) {
    char        dir[128];
    char        base[64];
    const char* slash;
    struct stat st;
    FILE*       fp;

    snprintf(actual, actualsz, "%s", path);

    fp = fopen(actual, "a");             /* 原路径能直接写就行（探路用 "a"，别清掉已存在的文件） */
    if (fp) { fclose(fp); return 0; }

    slash = strrchr(path, '/');
    if (!slash || slash == path) return -1;   /* 没有目录部分（或根目录），没得退回 */

    if ((size_t)(slash - path) >= sizeof(dir)) return -1;
    snprintf(dir,  sizeof(dir),  "%.*s", (int)(slash - path), path);
    snprintf(base, sizeof(base), "%s", slash + 1);
    if (base[0] == '\0') return -1;

    /* 目录不存在（stat 失败）就建一层出来，再试一次 */
    if (stat(dir, &st) != 0 && mkdir(dir, 0755) == 0) {
        fp = fopen(actual, "a");
        if (fp) { fclose(fp); return 0; }
    }

    /* 目录建不出来或建了仍然写不进去：退回当前目录的同名文件 */
    snprintf(actual, actualsz, "%s", base);
    fp = fopen(actual, "a");
    if (fp) { fclose(fp); return 0; }

    return -1;
}

/* 导出患者列表到文本文件 */
static void patient_export_flow(DB_HANDLE* db) {
    char filename[128];
    char actual[256];
    int ret;

    prompt("导出文件名（直接回车用默认 data/patients_export.txt）：",
           filename, sizeof(filename));
    if (filename[0] == '\0') {
        snprintf(filename, sizeof(filename), "data/patients_export.txt");
    }

    if (prepare_out_path(filename, actual, sizeof(actual)) != 0) {
        printf("导出失败：路径 %s 写不进去（目录不存在且建不出来，"
               "当前目录也写不了）\n", filename);
        return;
    }

    ret = patient_export(db, actual);
    if (ret == 0) {
        /* 打印绝对路径：程序常在 bin/ 下运行，只说相对路径容易让人找不到文件 */
        char full[512];
        if (strcmp(actual, filename) != 0) {
            printf("%s 写不进去，已改为导出到当前目录的 %s\n", filename, actual);
        }
        printf("已导出到 %s\n", realpath(actual, full) ? full : actual);
    } else if (ret == -3) {
        printf("导出失败：写文件出错（%s）\n", actual);
    } else {
        printf("导出失败：%s\n", err_text(ret));
    }
}

/* 患者档案管理子菜单 */
static void do_patient_menu(DB_HANDLE* db) {
    int running = 1;

    while (running) {
        int choice;
        int r;

        printf("\n--- 患者档案管理 ---\n");
        printf("  1. 查询全部患者\n");
        printf("  2. 按编号查询\n");
        printf("  3. 修改联系方式（电话/住址）\n");
        printf("  4. 注销患者\n");
        printf("  5. 导出患者列表\n");
        printf("  0. 返回\n");
        printf("请输入选项[0-5]：");
        fflush(stdout);

        r = read_choice(&choice);
        if (r == -2) return;
        if (r != 0) { printf("输入无效，请重新输入\n"); continue; }

        switch (choice) {
            case 1: patient_list(db);          break;
            case 2: patient_query(db);         break;
            case 3: patient_update(db);        break;
            case 4: patient_delete_flow(db);   break;
            case 5: patient_export_flow(db);   break;
            case 0: running = 0;               break;
            default: printf("无效选项\n");     break;
        }
    }
}

/* ==========================================================================
 * 2. 医生管理（增 / 查 / 改 / 离职）
 * ========================================================================== */

/* 查询医生列表 */
static void doctor_list(DB_HANDLE* db) {
    Doctor* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = doctor_query_all(db, &list, &count);
    if (ret != 0) {
        printf("[查询医生失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 医生列表（共 %d 人）===\n", count);
    if (count == 0) {
        printf("  暂无医生。\n");
        doctor_list_free(list);
        return;
    }

    {
        const int   w[] = {10, 12, 4, 12, 12, 14, 0};
        const char* cells[7];

        cells[0] = "医生编号"; cells[1] = "姓名"; cells[2] = "性别";
        cells[3] = "职称";     cells[4] = "科室"; cells[5] = "电话";
        cells[6] = "状态";
        table_row(cells, w, 7);

        for (i = 0; i < count; i++) {
            cells[0] = list[i].doctor_id;
            cells[1] = list[i].name;
            cells[2] = list[i].gender;
            cells[3] = list[i].title;
            cells[4] = list[i].dept;
            cells[5] = list[i].phone;
            cells[6] = list[i].status == 1 ? "在职" : "离职";
            table_row(cells, w, 7);
        }
    }

    doctor_list_free(list);
}

/* 按编号查询单个医生 */
static void doctor_query(DB_HANDLE* db) {
    char doctor_id[10];
    Doctor d;
    int ret;

    prompt("请输入医生编号（如 D001，0 返回）：", doctor_id, sizeof(doctor_id));
    if (doctor_id[0] == '\0' || strcmp(doctor_id, "0") == 0) return;

    memset(&d, 0, sizeof(d));
    ret = doctor_query_by_id(db, doctor_id, &d);
    if (ret == -4) {
        printf("没有编号为 %s 的医生，或该医生已离职。\n", doctor_id);
        return;
    } else if (ret != 0) {
        printf("查询失败：%s\n", err_text(ret));
        return;
    }

    printf("\n--- 医生信息 ---\n");
    printf("  医生编号：%s\n", d.doctor_id);
    printf("  姓名：    %s\n", d.name);
    printf("  性别：    %s\n", d.gender);
    printf("  职称：    %s\n", d.title[0] ? d.title : "（未填）");
    printf("  科室：    %s\n", d.dept[0] ? d.dept : "（未填）");
    printf("  电话：    %s\n", d.phone[0] ? d.phone : "（未填）");
    printf("  登录账号：%s\n", d.username);
    printf("  状态：    %s\n", d.status == 1 ? "在职" : "离职");
}

/* 新增医生 */
static void doctor_add_flow(DB_HANDLE* db) {
    Doctor d;
    int ret;

    memset(&d, 0, sizeof(d));
    d.status = 1;

    printf("\n=== 新增医生 ===\n");
    prompt("姓名：",           d.name,     sizeof(d.name));
    prompt("性别(男/女)：",    d.gender,   sizeof(d.gender));
    prompt("职称(如 主治医师)：", d.title, sizeof(d.title));
    prompt("科室(如 内科)：",  d.dept,     sizeof(d.dept));
    prompt("联系电话：",       d.phone,    sizeof(d.phone));
    prompt("登录密码：",       d.password, sizeof(d.password));

    ret = doctor_add(db, &d);
    if (ret == 0) {
        printf("新增成功，医生编号 %s（登录账号）\n", d.doctor_id);
    } else if (ret == -3) {
        printf("新增失败：编号或登录账号已存在\n");
    } else {
        printf("新增失败：%s\n", err_text(ret));
    }
}

/* 修改医生信息（职称/科室/电话，其余保留原值） */
static void doctor_update_flow(DB_HANDLE* db) {
    char doctor_id[10];
    char title[20];
    char dept[50];
    char phone[20];
    Doctor d;
    int ret;

    prompt("请输入医生编号（0 返回）：", doctor_id, sizeof(doctor_id));
    if (doctor_id[0] == '\0' || strcmp(doctor_id, "0") == 0) return;

    memset(&d, 0, sizeof(d));
    ret = doctor_query_by_id(db, doctor_id, &d);
    if (ret == -4) {
        printf("没有编号为 %s 的医生，或该医生已离职。\n", doctor_id);
        return;
    } else if (ret != 0) {
        printf("查询失败：%s\n", err_text(ret));
        return;
    }

    printf("\n当前信息：职称[%s] 科室[%s] 电话[%s]\n",
           d.title[0] ? d.title : "（未填）",
           d.dept[0] ? d.dept : "（未填）",
           d.phone[0] ? d.phone : "（未填）");
    printf("直接回车即保留原值。\n");

    snprintf(title, sizeof(title), "%s", d.title);
    snprintf(dept,  sizeof(dept),  "%s", d.dept);
    snprintf(phone, sizeof(phone), "%s", d.phone);

    prompt("职称(如 主治医师)：", title, sizeof(title));
    prompt("科室(如 内科)：",     dept,  sizeof(dept));
    prompt("联系电话：",          phone, sizeof(phone));

    keep_or(d.title, title, sizeof(title));
    keep_or(d.dept,  dept,  sizeof(dept));
    keep_or(d.phone, phone, sizeof(phone));

    snprintf(d.title, sizeof(d.title), "%s", title);
    snprintf(d.dept,  sizeof(d.dept),  "%s", dept);
    snprintf(d.phone, sizeof(d.phone), "%s", phone);

    ret = doctor_update(db, &d);
    if (ret == 0) {
        printf("修改成功。\n");
    } else if (ret == -4) {
        printf("修改失败：医生不存在或已离职\n");
    } else {
        printf("修改失败：%s\n", err_text(ret));
    }
}

/* 医生离职（软删除） */
static void doctor_delete_flow(DB_HANDLE* db) {
    char doctor_id[10];
    char ans[16];
    int ret;

    prompt("请输入要办理离职的医生编号（0 返回）：", doctor_id, sizeof(doctor_id));
    if (doctor_id[0] == '\0' || strcmp(doctor_id, "0") == 0) return;

    prompt("确认该医生离职？离职后不能登录。(y/N)：", ans, sizeof(ans));
    if (ans[0] != 'y' && ans[0] != 'Y') {
        printf("已取消。\n");
        return;
    }

    ret = doctor_delete(db, doctor_id);
    if (ret == 0) {
        printf("医生 %s 已办理离职。\n", doctor_id);
    } else if (ret == -4) {
        printf("办理失败：医生不存在或已离职\n");
    } else {
        printf("办理失败：%s\n", err_text(ret));
    }
}

/* 医生管理子菜单 */
static void do_doctor_menu(DB_HANDLE* db) {
    int running = 1;

    while (running) {
        int choice;
        int r;

        printf("\n--- 医生管理 ---\n");
        printf("  1. 查询医生列表\n");
        printf("  2. 按编号查询\n");
        printf("  3. 新增医生\n");
        printf("  4. 修改医生信息\n");
        printf("  5. 办理离职\n");
        printf("  0. 返回\n");
        printf("请输入选项[0-5]：");
        fflush(stdout);

        r = read_choice(&choice);
        if (r == -2) return;
        if (r != 0) { printf("输入无效，请重新输入\n"); continue; }

        switch (choice) {
            case 1: doctor_list(db);        break;
            case 2: doctor_query(db);       break;
            case 3: doctor_add_flow(db);    break;
            case 4: doctor_update_flow(db); break;
            case 5: doctor_delete_flow(db); break;
            case 0: running = 0;            break;
            default: printf("无效选项\n");  break;
        }
    }
}

/* ==========================================================================
 * 6. 药品管理（盘点 / 查 / 增 / 入库 / 出库 / 调价）
 * ========================================================================== */

/* 药品盘点：列出全部药品，库存低于 50 的标注"库存不足" */
static void medicine_list(DB_HANDLE* db) {
    Medicine* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = medicine_query_all(db, &list, &count);
    if (ret != 0) {
        printf("[查询药品失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 药品盘点（共 %d 种）===\n", count);
    if (count == 0) {
        printf("  药品目录为空。\n");
        medicine_list_free(list);
        return;
    }

    {
        const int   w[] = {10, 18, 16, -6, -8, -7, 0};
        const char* cells[7];
        char        price_txt[16];
        char        stock_txt[16];
        char        man_txt[160];

        cells[0] = "药品编号"; cells[1] = "名称"; cells[2] = "规格";
        cells[3] = "单位";     cells[4] = "单价"; cells[5] = "库存";
        cells[6] = "厂家";
        table_row(cells, w, 7);

        for (i = 0; i < count; i++) {
            snprintf(price_txt, sizeof(price_txt), "%.2f", list[i].price);
            snprintf(stock_txt, sizeof(stock_txt), "%d", list[i].stock);
            /* 库存预警标注跟在最后一列厂家后面，拼成一个字符串一起打印 */
            snprintf(man_txt, sizeof(man_txt), "%s%s", list[i].manufacturer,
                     list[i].stock < 50 ? "  [库存不足]" : "");
            cells[0] = list[i].med_id;
            cells[1] = list[i].med_name;
            cells[2] = list[i].spec;
            cells[3] = list[i].unit;
            cells[4] = price_txt;
            cells[5] = stock_txt;
            cells[6] = man_txt;
            table_row(cells, w, 7);
        }
    }

    medicine_list_free(list);
}

/* 按编号查询单个药品 */
static void medicine_query(DB_HANDLE* db) {
    char med_id[20];
    Medicine m;
    int ret;

    prompt("请输入药品编号（如 M001，0 返回）：", med_id, sizeof(med_id));
    if (med_id[0] == '\0' || strcmp(med_id, "0") == 0) return;

    memset(&m, 0, sizeof(m));
    ret = medicine_query_by_id(db, med_id, &m);
    if (ret == -4) {
        printf("没有编号为 %s 的药品。\n", med_id);
        return;
    } else if (ret != 0) {
        printf("查询失败：%s\n", err_text(ret));
        return;
    }

    printf("\n--- 药品信息 ---\n");
    printf("  药品编号：%s\n", m.med_id);
    printf("  名称：    %s\n", m.med_name);
    printf("  规格：    %s\n", m.spec);
    printf("  单位：    %s\n", m.unit);
    printf("  单价：    %.2f\n", m.price);
    printf("  库存：    %d%s\n", m.stock, m.stock < 50 ? "（库存不足）" : "");
    printf("  厂家：    %s\n", m.manufacturer);
}

/* 新增药品 */
static void medicine_add_flow(DB_HANDLE* db) {
    Medicine m;
    int ret;

    memset(&m, 0, sizeof(m));

    printf("\n=== 新增药品 ===\n");
    prompt("药品编号(如 M002)：",  m.med_id,       sizeof(m.med_id));
    prompt("药品名称：",            m.med_name,     sizeof(m.med_name));
    prompt("规格(如 0.25g*24粒)：", m.spec,         sizeof(m.spec));
    prompt("单位(如 盒)：",         m.unit,         sizeof(m.unit));
    prompt_double("单价：",         &m.price);
    prompt_int("初始库存：",        &m.stock);
    prompt("生产厂家：",            m.manufacturer, sizeof(m.manufacturer));

    if (m.med_id[0] == '\0') {
        printf("药品编号不能为空，已取消。\n");
        return;
    }
    if (m.price < 0) {
        printf("单价不能为负，已取消。\n");
        return;
    }
    if (m.stock < 0) {
        printf("库存不能为负，已取消。\n");
        return;
    }

    ret = medicine_add(db, &m);
    if (ret == 0) {
        printf("新增成功。\n");
    } else if (ret == -3) {
        printf("新增失败：药品编号已存在\n");
    } else {
        printf("新增失败：%s\n", err_text(ret));
    }
}

/* 入库 */
static void medicine_stock_in_flow(DB_HANDLE* db) {
    char med_id[20];
    int quantity;
    int ret;

    prompt("药品编号（0 返回）：", med_id, sizeof(med_id));
    if (med_id[0] == '\0' || strcmp(med_id, "0") == 0) return;

    prompt_int("入库数量：", &quantity);
    if (quantity <= 0) {
        printf("入库数量必须大于 0，已取消。\n");
        return;
    }

    ret = medicine_stock_in(db, med_id, quantity);
    if (ret == 0) {
        printf("入库成功，%s 库存 +%d\n", med_id, quantity);
    } else if (ret == -4) {
        printf("入库失败：药品不存在\n");
    } else {
        printf("入库失败：%s\n", err_text(ret));
    }
}

/* 出库 */
static void medicine_stock_out_flow(DB_HANDLE* db) {
    char med_id[20];
    int quantity;
    int ret;

    prompt("药品编号（0 返回）：", med_id, sizeof(med_id));
    if (med_id[0] == '\0' || strcmp(med_id, "0") == 0) return;

    prompt_int("出库数量：", &quantity);
    if (quantity <= 0) {
        printf("出库数量必须大于 0，已取消。\n");
        return;
    }

    ret = medicine_stock_out(db, med_id, quantity);
    if (ret == 0) {
        printf("出库成功，%s 库存 -%d\n", med_id, quantity);
    } else if (ret == -4) {
        printf("出库失败：药品不存在\n");
    } else if (ret == -5) {
        printf("出库失败：库存不足，拒绝出库\n");
    } else {
        printf("出库失败：%s\n", err_text(ret));
    }
}

/* 删除药品（被历史处方引用过的不让删，保住处方明细） */
static void medicine_delete_flow(DB_HANDLE* db) {
    char med_id[20];
    char ans[16];
    int ret;

    prompt("药品编号（0 返回）：", med_id, sizeof(med_id));
    if (med_id[0] == '\0' || strcmp(med_id, "0") == 0) return;

    prompt("确认删除该药品？删除后不可恢复。(y/N)：", ans, sizeof(ans));
    if (ans[0] != 'y' && ans[0] != 'Y') {
        printf("已取消。\n");
        return;
    }

    ret = medicine_delete(db, med_id);
    if (ret == 0) {
        printf("药品 %s 已删除。\n", med_id);
    } else if (ret == -4) {
        printf("删除失败：药品不存在\n");
    } else if (ret == -6) {
        printf("删除失败：该药品已出现在历史处方里，不能删除\n");
    } else {
        printf("删除失败：%s\n", err_text(ret));
    }
}

/* 调价 */
static void medicine_price_flow(DB_HANDLE* db) {
    char med_id[20];
    double new_price;
    int ret;

    prompt("药品编号（0 返回）：", med_id, sizeof(med_id));
    if (med_id[0] == '\0' || strcmp(med_id, "0") == 0) return;

    prompt_double("新单价：", &new_price);
    if (new_price < 0) {
        printf("单价不能为负，已取消。\n");
        return;
    }

    ret = medicine_update_price(db, med_id, new_price);
    if (ret == 0) {
        printf("调价成功，%s 单价改为 %.2f\n", med_id, new_price);
    } else if (ret == -4) {
        printf("调价失败：药品不存在\n");
    } else {
        printf("调价失败：%s\n", err_text(ret));
    }
}

/* 药品管理子菜单 */
static void do_medicine_menu(DB_HANDLE* db) {
    int running = 1;

    while (running) {
        int choice;
        int r;

        printf("\n--- 药品管理 ---\n");
        printf("  1. 药品盘点（查询全部）\n");
        printf("  2. 按编号查询\n");
        printf("  3. 新增药品\n");
        printf("  4. 入库\n");
        printf("  5. 出库\n");
        printf("  6. 调价\n");
        printf("  7. 删除药品\n");
        printf("  0. 返回\n");
        printf("请输入选项[0-7]：");
        fflush(stdout);

        r = read_choice(&choice);
        if (r == -2) return;
        if (r != 0) { printf("输入无效，请重新输入\n"); continue; }

        switch (choice) {
            case 1: medicine_list(db);           break;
            case 2: medicine_query(db);          break;
            case 3: medicine_add_flow(db);       break;
            case 4: medicine_stock_in_flow(db);  break;
            case 5: medicine_stock_out_flow(db); break;
            case 6: medicine_price_flow(db);     break;
            case 7: medicine_delete_flow(db);    break;
            case 0: running = 0;                 break;
            default: printf("无效选项\n");       break;
        }
    }
}

/* ==========================================================================
 * 3/4/5/7 查询类：预约 / 就诊记录 / 处方 / 病历
 * ========================================================================== */

/* 3. 预约查询 */
static void do_appointment_query(DB_HANDLE* db) {
    Appointment* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = appointment_query_all(db, &list, &count);
    if (ret != 0) {
        printf("[查询预约失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 全部预约（共 %d 条）===\n", count);
    if (count == 0) {
        printf("  暂无预约记录。\n");
        appointment_list_free(list);
        return;
    }

    {
        const int   w[] = {-8, 12, 10, 12, 6, 8, 0};
        const char* cells[7];
        char        id_txt[16];

        cells[0] = "预约ID"; cells[1] = "患者编号"; cells[2] = "医生编号";
        cells[3] = "日期";   cells[4] = "时段";     cells[5] = "状态";
        cells[6] = "症状";
        table_row(cells, w, 7);

        for (i = 0; i < count; i++) {
            snprintf(id_txt, sizeof(id_txt), "%d", list[i].appoint_id);
            cells[0] = id_txt;
            cells[1] = list[i].patient_id;
            cells[2] = list[i].doctor_id;
            cells[3] = list[i].appoint_date;
            cells[4] = list[i].time_slot[0] ? list[i].time_slot : "未填";
            cells[5] = appt_status_text(list[i].status);
            cells[6] = list[i].symptom[0] ? list[i].symptom : "（未填）";
            table_row(cells, w, 7);
        }
    }

    appointment_list_free(list);
}

/* 4. 就诊记录查询 */
static void do_visit_query(DB_HANDLE* db) {
    VisitRecord* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = visit_query_all(db, &list, &count);
    if (ret != 0) {
        printf("[查询就诊记录失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 全部就诊记录（共 %d 条）===\n", count);
    if (count == 0) {
        printf("  暂无就诊记录。\n");
        visit_list_free(list);
        return;
    }

    for (i = 0; i < count; i++) {
        printf("----------------------------------------\n");
        printf("  就诊ID %d   预约ID %d   患者 %s   医生 %s\n",
               list[i].visit_id, list[i].appoint_id, list[i].patient_id, list[i].doctor_id);
        printf("  就诊时间 %s\n", list[i].visit_time[0] ? list[i].visit_time : "未知");
        printf("  主诉     %s\n", list[i].chief_complaint[0] ? list[i].chief_complaint : "（未填）");
        printf("  现病史   %s\n", list[i].present_illness[0] ? list[i].present_illness : "（未填）");
        if (list[i].temp > 0) printf("  体温     %.1f ℃\n", list[i].temp);
        else                  printf("  体温     （未填）\n");
        printf("  血压     %s\n", list[i].blood_pressure[0] ? list[i].blood_pressure : "（未填）");
        if (list[i].heart_rate > 0) printf("  心率     %d 次/分\n", list[i].heart_rate);
        else                        printf("  心率     （未填）\n");
    }
    printf("----------------------------------------\n");

    visit_list_free(list);
}

/* 5. 处方查询 */
static void do_prescription_query(DB_HANDLE* db) {
    Prescription* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = prescription_query_all(db, &list, &count);
    if (ret != 0) {
        printf("[查询处方失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 全部处方（共 %d 张）===\n", count);
    if (count == 0) {
        printf("  暂无处方。\n");
        prescription_list_free(list);
        return;
    }

    {
        const int   w[] = {-8, -8, 12, 10, 8, 0};
        const char* cells[6];
        char        presc_txt[16];
        char        visit_txt[16];

        cells[0] = "处方ID"; cells[1] = "就诊ID"; cells[2] = "患者编号";
        cells[3] = "医生编号"; cells[4] = "状态"; cells[5] = "诊断";
        table_row(cells, w, 6);

        for (i = 0; i < count; i++) {
            snprintf(presc_txt, sizeof(presc_txt), "%d", list[i].presc_id);
            snprintf(visit_txt, sizeof(visit_txt), "%d", list[i].visit_id);
            cells[0] = presc_txt;
            cells[1] = visit_txt;
            cells[2] = list[i].patient_id;
            cells[3] = list[i].doctor_id;
            cells[4] = presc_status_text(list[i].status);
            cells[5] = list[i].diagnosis[0] ? list[i].diagnosis : "（未填）";
            table_row(cells, w, 6);
        }
    }

    prescription_list_free(list);
}

/* 7. 病历查询 */
static void do_medical_record_query(DB_HANDLE* db) {
    MedicalRecord* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = medical_record_query_all(db, &list, &count);
    if (ret != 0) {
        printf("[查询病历失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 全部病历（共 %d 份）===\n", count);
    if (count == 0) {
        printf("  暂无病历。\n");
        medical_record_list_free(list);
        return;
    }

    for (i = 0; i < count; i++) {
        printf("----------------------------------------\n");
        printf("  病历ID %d   就诊ID %d   患者 %s   医生 %s\n",
               list[i].record_id, list[i].visit_id, list[i].patient_id, list[i].doctor_id);
        printf("  过敏史   %s\n", list[i].allergy_history[0] ? list[i].allergy_history : "（无）");
        printf("  既往史   %s\n", list[i].past_history[0] ? list[i].past_history : "（未填）");
        printf("  家族史   %s\n", list[i].family_history[0] ? list[i].family_history : "（未填）");
        printf("  更新时间 %s\n", list[i].update_time[0] ? list[i].update_time : "未知");
    }
    printf("----------------------------------------\n");

    medical_record_list_free(list);
}

/* ==========================================================================
 * 8. 修改密码
 * ========================================================================== */
static void do_change_password(DB_HANDLE* db, const LoginUser* user) {
    char old_pwd[64];
    char new_pwd[64];
    char confirm[64];
    int ret;

    prompt("原密码：",     old_pwd, sizeof(old_pwd));
    prompt("新密码：",     new_pwd, sizeof(new_pwd));
    prompt("确认新密码：", confirm, sizeof(confirm));

    if (new_pwd[0] == '\0') {
        printf("新密码不能为空，已取消。\n");
        return;
    }
    if (strcmp(new_pwd, confirm) != 0) {
        printf("两次输入的新密码不一致，已取消。\n");
        return;
    }
    if (strcmp(old_pwd, new_pwd) == 0) {
        printf("新密码和原密码相同，已取消。\n");
        return;
    }

    ret = user_change_password(db, user->role, user->id, old_pwd, new_pwd);
    if (ret == 0) {
        printf("密码修改成功，下次登录请用新密码。\n");
    } else if (ret == -4) {
        printf("修改失败：原密码错误\n");
    } else {
        printf("修改失败：%s\n", err_text(ret));
    }
}

/* ==========================================================================
 * 管理员主循环
 * ========================================================================== */
void menu_admin(DB_HANDLE* db, const LoginUser* user) {
    int running = 1;

    /* 角色 1 = 管理员，越权直接拒绝（教程 1.6 安全需求：越权操作直接拒绝） */
    if (!db || !user || user->role != 1) return;

    printf("\n========================================\n");
    printf("  管理员主菜单  %s(%s)\n", user->name, user->id);
    printf("========================================\n");

    while (running) {
        int choice;
        int r;

        printf("\n========================================\n");
        printf("  1. 患者档案管理\n");
        printf("  2. 医生管理\n");
        printf("  3. 预约查询\n");
        printf("  4. 就诊记录查询\n");
        printf("  5. 处方查询\n");
        printf("  6. 药品管理\n");
        printf("  7. 病历查询\n");
        printf("  8. 修改密码\n");
        printf("  0. 退出系统\n");
        printf("========================================\n");
        printf("请输入选项[0-8]：");
        fflush(stdout);

        r = read_choice(&choice);
        if (r == -2) return;                     /* 输入结束，直接返回，避免空转 */
        if (r != 0) { printf("输入无效，请重新输入\n"); continue; }

        switch (choice) {
            case 1: do_patient_menu(db);            break;
            case 2: do_doctor_menu(db);             break;
            case 3: do_appointment_query(db);       break;
            case 4: do_visit_query(db);             break;
            case 5: do_prescription_query(db);      break;
            case 6: do_medicine_menu(db);           break;
            case 7: do_medical_record_query(db);    break;
            case 8: do_change_password(db, user);   break;
            case 0: running = 0;                    break;
            default: printf("无效选项\n");          break;
        }
    }

    printf("\n已退出系统。\n");
}
