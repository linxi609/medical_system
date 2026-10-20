
#include "menu.h"
#include "patient.h"
#include "doctor.h"
#include "medicine.h"
#include "appointment.h"
#include "visit.h"
#include "prescription.h"
#include "medical_record.h"
#include "utils.h"
#include "menu_common.h"
#include "cli_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>





















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

static void patient_query(DB_HANDLE* db) {
    char patient_id[20];
    Patient p;
    int ret;

    cli_prompt("请输入患者编号（如 P20260001，0 返回）：", patient_id, sizeof(patient_id));
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

static void patient_update(DB_HANDLE* db) {
    char patient_id[20];
    char phone[20];
    char address[200];
    int ret;

    cli_prompt("请输入患者编号（0 返回）：", patient_id, sizeof(patient_id));
    if (patient_id[0] == '\0' || strcmp(patient_id, "0") == 0) return;

    cli_prompt("新电话：", phone, sizeof(phone));
    cli_prompt("新住址：", address, sizeof(address));

    ret = patient_update_self(db, patient_id, phone, address);
    if (ret == 0) {
        printf("修改成功。\n");
    } else if (ret == -4) {
        printf("修改失败：患者不存在或已注销\n");
    } else {
        printf("修改失败：%s\n", err_text(ret));
    }
}

static void patient_delete_flow(DB_HANDLE* db) {
    char patient_id[20];
    char ans[16];
    int ret;

    cli_prompt("请输入要注销的患者编号（0 返回）：", patient_id, sizeof(patient_id));
    if (patient_id[0] == '\0' || strcmp(patient_id, "0") == 0) return;

    cli_prompt("确认注销该患者？注销后不能登录、不能挂号。(y/N)：", ans, sizeof(ans));
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

static int prepare_out_path(const char* path, char* actual, size_t actualsz) {
    char        dir[128];
    char        base[64];
    const char* slash;
    struct stat st;
    FILE*       fp;

    snprintf(actual, actualsz, "%s", path);

    fp = fopen(actual, "a");
    if (fp) { fclose(fp); return 0; }

    slash = strrchr(path, '/');
    if (!slash || slash == path) return -1;

    if ((size_t)(slash - path) >= sizeof(dir)) return -1;
    snprintf(dir,  sizeof(dir),  "%.*s", (int)(slash - path), path);
    snprintf(base, sizeof(base), "%s", slash + 1);
    if (base[0] == '\0') return -1;

    if (stat(dir, &st) != 0 && mkdir(dir, 0755) == 0) {
        fp = fopen(actual, "a");
        if (fp) { fclose(fp); return 0; }
    }

    snprintf(actual, actualsz, "%s", base);
    fp = fopen(actual, "a");
    if (fp) { fclose(fp); return 0; }

    return -1;
}

static void patient_export_flow(DB_HANDLE* db) {
    char filename[128];
    char actual[256];
    int ret;

    cli_prompt("导出文件名（直接回车用默认 data/patients_export.txt）：",
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

        r = cli_read_choice(&choice);
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

static void doctor_query(DB_HANDLE* db) {
    char doctor_id[10];
    Doctor d;
    int ret;

    cli_prompt("请输入医生编号（如 D001，0 返回）：", doctor_id, sizeof(doctor_id));
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

static void doctor_add_flow(DB_HANDLE* db) {
    Doctor d;
    int ret;

    memset(&d, 0, sizeof(d));
    d.status = 1;

    printf("\n=== 新增医生 ===\n");
    cli_prompt("姓名：", d.name, sizeof(d.name));
    if (cli_prompt_chinese("性别(男/女)：",       d.gender, sizeof(d.gender), 0) != 0 ||
        cli_prompt_chinese("职称(如 主治医师)：", d.title,  sizeof(d.title),  1) != 0 ||
        cli_prompt_chinese("科室(如 内科)：",     d.dept,   sizeof(d.dept),   1) != 0 ||
        cli_prompt_digits ("联系电话：",          d.phone,  sizeof(d.phone),  1) != 0) {
        printf("输入结束，已取消新增。\n");
        return;
    }
    cli_prompt("登录密码：", d.password, sizeof(d.password));

    ret = doctor_add(db, &d);
    if (ret == 0) {
        printf("新增成功，医生编号 %s（登录账号）\n", d.doctor_id);
    } else if (ret == -3) {
        printf("新增失败：编号或登录账号已存在\n");
    } else {
        printf("新增失败：%s\n", err_text(ret));
    }
}

static void doctor_update_flow(DB_HANDLE* db) {
    char doctor_id[10];
    char title[20];
    char dept[50];
    char phone[20];
    Doctor d;
    int ret;

    cli_prompt("请输入医生编号（0 返回）：", doctor_id, sizeof(doctor_id));
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

    if (cli_prompt_chinese("职称(如 主治医师)：", title, sizeof(title), 1) != 0 ||
        cli_prompt_chinese("科室(如 内科)：",     dept,  sizeof(dept),  1) != 0 ||
        cli_prompt_digits ("联系电话：",          phone, sizeof(phone), 1) != 0) {
        printf("输入结束，已取消修改。\n");
        return;
    }

    cli_keep_or(d.title, title, sizeof(title));
    cli_keep_or(d.dept,  dept,  sizeof(dept));
    cli_keep_or(d.phone, phone, sizeof(phone));

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

static void doctor_delete_flow(DB_HANDLE* db) {
    char doctor_id[10];
    char ans[16];
    int ret;

    cli_prompt("请输入要办理离职的医生编号（0 返回）：", doctor_id, sizeof(doctor_id));
    if (doctor_id[0] == '\0' || strcmp(doctor_id, "0") == 0) return;

    cli_prompt("确认该医生离职？离职后不能登录。(y/N)：", ans, sizeof(ans));
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

        r = cli_read_choice(&choice);
        if (r == -2) return;
        if (r != 0 || choice < 0 || choice > 5) {
            printf("输入有误，请输入 0-5 之间的序号\n");
            continue;
        }

        switch (choice) {
            case 1: doctor_list(db);        break;
            case 2: doctor_query(db);       break;
            case 3: doctor_add_flow(db);    break;
            case 4: doctor_update_flow(db); break;
            case 5: doctor_delete_flow(db); break;
            case 0: running = 0;            break;
            default: printf("输入有误，请输入 0-5 之间的序号\n"); break;
        }
    }
}


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

static void medicine_query(DB_HANDLE* db) {
    char med_id[20];
    Medicine m;
    int ret;

    cli_prompt("请输入药品编号（如 M001，0 返回）：", med_id, sizeof(med_id));
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

static void medicine_add_flow(DB_HANDLE* db) {
    Medicine m;
    int ret;

    memset(&m, 0, sizeof(m));

    printf("\n=== 新增药品 ===\n");
    cli_prompt("药品名称：",            m.med_name,     sizeof(m.med_name));
    cli_prompt("规格(如 0.25g*24粒)：", m.spec,         sizeof(m.spec));
    cli_prompt("单位(如 盒)：",         m.unit,         sizeof(m.unit));
    cli_prompt_double("单价：",         &m.price);
    cli_prompt_int("初始库存：",        &m.stock);
    cli_prompt("生产厂家：",            m.manufacturer, sizeof(m.manufacturer));

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
        printf("新增成功，药品编号 %s（已自动生成）\n", m.med_id);
    } else if (ret == -3) {
        printf("新增失败：药品编号已存在，请重试\n");
    } else {
        printf("新增失败：%s\n", err_text(ret));
    }
}

static void medicine_stock_in_flow(DB_HANDLE* db) {
    char med_id[20];
    int quantity;
    int ret;

    cli_prompt("药品编号（0 返回）：", med_id, sizeof(med_id));
    if (med_id[0] == '\0' || strcmp(med_id, "0") == 0) return;

    cli_prompt_int("入库数量：", &quantity);
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

static void medicine_stock_out_flow(DB_HANDLE* db) {
    char med_id[20];
    int quantity;
    int ret;

    cli_prompt("药品编号（0 返回）：", med_id, sizeof(med_id));
    if (med_id[0] == '\0' || strcmp(med_id, "0") == 0) return;

    cli_prompt_int("出库数量：", &quantity);
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

static void medicine_delete_flow(DB_HANDLE* db) {
    char med_id[20];
    char ans[16];
    int ret;

    cli_prompt("药品编号（0 返回）：", med_id, sizeof(med_id));
    if (med_id[0] == '\0' || strcmp(med_id, "0") == 0) return;

    cli_prompt("确认删除该药品？删除后不可恢复。(y/N)：", ans, sizeof(ans));
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

static void medicine_price_flow(DB_HANDLE* db) {
    char med_id[20];
    double new_price;
    int ret;

    cli_prompt("药品编号（0 返回）：", med_id, sizeof(med_id));
    if (med_id[0] == '\0' || strcmp(med_id, "0") == 0) return;

    cli_prompt_double("新单价：", &new_price);
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

        r = cli_read_choice(&choice);
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



void menu_admin(DB_HANDLE* db, const LoginUser* user) {
    int running = 1;

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

        r = cli_read_choice(&choice);
        if (r == -2) return;
        if (r != 0) { printf("输入无效，请重新输入\n"); continue; }

        switch (choice) {
            case 1: do_patient_menu(db);            break;
            case 2: do_doctor_menu(db);             break;
            case 3: do_appointment_query(db);       break;
            case 4: do_visit_query(db);             break;
            case 5: do_prescription_query(db);      break;
            case 6: do_medicine_menu(db);           break;
            case 7: do_medical_record_query(db);    break;
            case 8: menu_change_password(db, user);   break;
            case 0: running = 0;                    break;
            default: printf("无效选项\n");          break;
        }
    }

    printf("\n已退出系统。\n");
}
