/* ==========================================================================
 * menu_patient.c —— 患者工作台 实现
 * --------------------------------------------------------------------------
 * 负责组：患者组
 * 对应头文件：inc/menu.h（菜单项清单和各自该调哪个函数，见 menu_patient 的注释）
 * 对应界面原型：教程 1.7 "患者工作台"
 *
 * 菜单项：
 *   1. 挂号          -> doctor_query_all 选医生，appointment_add 挂号
 *   2. 我的预约      -> appointment_query_by_patient / appointment_cancel
 *   3. 我的就诊记录  -> visit_query_by_patient
 *   4. 我的处方      -> prescription_query_by_patient / prescription_query_by_id
 *   5. 我的病历      -> medical_record_query_by_patient
 *   6. 修改个人资料  -> patient_update_self / user_change_password
 *   0. 返回登录
 *
 * 说明：
 *   - 输入一律按 main.c 里 read_line 的写法读整行，不用 scanf("%d")，
 *     避免 scanf 遇到字母卡住、以及残留字符窜到下一次输入里。
 *   - 挂号日期格式 YYYY-MM-DD，只允许挂今天和以后的号；
 *     时段固定 "上午" / "下午" 两档。
 *   - 所有失败分支按各模块注释里约定的返回码给出中文提示。
 * ========================================================================== */

#include "menu.h"
#include "doctor.h"
#include "patient.h"
#include "appointment.h"
#include "visit.h"
#include "prescription.h"
#include "medical_record.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* 读一个菜单选项。返回 0 成功，-1 输入非法，-2 输入结束（Ctrl+D 或管道读完） */
static int read_choice(int* out) {
    char buf[32];
    if (read_line(buf, sizeof(buf)) != 0) return -2;
    if (sscanf(buf, "%d", out) != 1) return -1;
    return 0;
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

/* 预约状态：0 待就诊，1 已就诊，2 已取消 */
static const char* appoint_status_text(int status) {
    switch (status) {
        case 0:  return "待就诊";
        case 1:  return "已就诊";
        case 2:  return "已取消";
        default: return "未知";
    }
}

/* 处方状态：1 有效 / 0 已作废 */
static const char* presc_status_text(int status) {
    return status == 1 ? "有效" : "已作废";
}

/* 校验日期格式 YYYY-MM-DD，且不能早于今天。1 合法，0 不合法 */
static int date_ok(const char* s) {
    int y, m, d;
    char today[12];

    if (!s || strlen(s) != 10) return 0;
    if (s[4] != '-' || s[7] != '-') return 0;
    if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) return 0;
    if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return 0;

    /* 和当天比：字符串形式的 YYYY-MM-DD 可直接按字典序比较 */
    {
        time_t now = time(NULL);
        struct tm* tm_now = localtime(&now);
        if (!tm_now) return 0;
        snprintf(today, sizeof(today), "%04d-%02d-%02d",
                 tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday);
    }
    return strcmp(s, today) >= 0;
}

/* ==========================================================================
 * 1. 挂号
 * ========================================================================== */
static void do_appoint(DB_HANDLE* db, const LoginUser* user) {
    Doctor* doctors = NULL;
    int     count = 0;
    int     ret;
    int     i;
    int     pick;
    Appointment a;

    /* ---------- 列出在职医生供选择 ---------- */
    ret = doctor_query_all(db, &doctors, &count);
    if (ret != 0) {
        printf("[查询医生列表失败：%s]\n", err_text(ret));
        return;
    }
    if (count == 0) {
        printf("目前没有可预约的医生，请稍后再试。\n");
        doctor_list_free(doctors);
        return;
    }

    printf("\n=== 可预约医生 ===\n");
    {
        const int   w[] = {-4, 10, 12, 6, 12, 0};
        const char* cells[6];
        char        seq_txt[16];

        cells[0] = "序号"; cells[1] = "编号"; cells[2] = "姓名";
        cells[3] = "性别"; cells[4] = "职称"; cells[5] = "科室";
        table_row(cells, w, 6);

        for (i = 0; i < count; i++) {
            snprintf(seq_txt, sizeof(seq_txt), "%d", i + 1);
            cells[0] = seq_txt;
            cells[1] = doctors[i].doctor_id;
            cells[2] = doctors[i].name;
            cells[3] = doctors[i].gender;
            cells[4] = doctors[i].title;
            cells[5] = doctors[i].dept;
            table_row(cells, w, 6);
        }
    }

    prompt_int("请选择医生序号（0 取消）：", &pick);
    if (pick <= 0) {
        printf("已取消挂号。\n");
        doctor_list_free(doctors);
        return;
    }
    if (pick > count) {
        printf("序号超出范围，已取消挂号。\n");
        doctor_list_free(doctors);
        return;
    }

    memset(&a, 0, sizeof(a));
    snprintf(a.patient_id, sizeof(a.patient_id), "%s", user->id);
    snprintf(a.doctor_id,  sizeof(a.doctor_id),  "%s", doctors[pick - 1].doctor_id);
    doctor_list_free(doctors);

    /* ---------- 日期与时段 ---------- */
    prompt("预约日期（格式 YYYY-MM-DD，只能挂今天及以后）：",
           a.appoint_date, sizeof(a.appoint_date));
    if (!date_ok(a.appoint_date)) {
        printf("日期格式不正确或早于今天，已取消挂号。\n");
        return;
    }

    printf("时段：1. 上午   2. 下午\n");
    prompt_int("请选择时段[1-2]：", &pick);
    if (pick == 1)      snprintf(a.time_slot, sizeof(a.time_slot), "上午");
    else if (pick == 2) snprintf(a.time_slot, sizeof(a.time_slot), "下午");
    else {
        printf("时段选择无效，已取消挂号。\n");
        return;
    }

    prompt("症状描述：", a.symptom, sizeof(a.symptom));

    /* ---------- 提交 ---------- */
    ret = appointment_add(db, &a);
    if (ret == 0) {
        printf("\n挂号成功！预约编号 %d，%s %s，医生 %s\n",
               a.appoint_id, a.appoint_date, a.time_slot, a.doctor_id);
        printf("挂号时间：%s\n", a.create_time[0] ? a.create_time : "已记录");
    } else if (ret == -8) {
        printf("\n挂号失败：你在 %s 已经约过这位医生了（取消后才能重挂）\n",
               a.appoint_date);
    } else {
        printf("\n挂号失败：%s\n", err_text(ret));
    }
}

/* ==========================================================================
 * 2. 我的预约（可取消待就诊的）
 * ========================================================================== */
static void do_my_appointments(DB_HANDLE* db, const LoginUser* user) {
    Appointment* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = appointment_query_by_patient(db, user->id, &list, &count);
    if (ret != 0) {
        printf("[查询失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 我的预约（共 %d 条）===\n", count);
    if (count == 0) {
        printf("  你还没有预约记录。\n");
        appointment_list_free(list);
        return;
    }

    {
        const int   w[] = {-8, 10, 12, 6, 8, 0};
        const char* cells[6];
        char        id_txt[16];

        cells[0] = "预约ID"; cells[1] = "医生"; cells[2] = "日期";
        cells[3] = "时段";   cells[4] = "状态"; cells[5] = "症状";
        table_row(cells, w, 6);

        for (i = 0; i < count; i++) {
            snprintf(id_txt, sizeof(id_txt), "%d", list[i].appoint_id);
            cells[0] = id_txt;
            cells[1] = list[i].doctor_id;
            cells[2] = list[i].appoint_date;
            cells[3] = list[i].time_slot;
            cells[4] = appoint_status_text(list[i].status);
            cells[5] = list[i].symptom[0] ? list[i].symptom : "（未填）";
            table_row(cells, w, 6);
        }
    }

    /* 循环处理取消，免得每取消一条都要重进一次菜单 */
    for (;;) {
        int appoint_id = 0;
        char ans[16];

        prompt_int("\n输入要取消的预约ID（0 结束）：", &appoint_id);
        if (appoint_id <= 0) break;

        for (i = 0; i < count; i++) {
            if (list[i].appoint_id == appoint_id) break;
        }
        if (i >= count) {
            printf("预约ID %d 不在你的预约列表里，请核对。\n", appoint_id);
            continue;
        }
        if (list[i].status != 0) {
            printf("该预约已是\"%s\"，不能取消。\n", appoint_status_text(list[i].status));
            continue;
        }

        prompt("确认取消这条预约？(y/N)：", ans, sizeof(ans));
        if (ans[0] != 'y' && ans[0] != 'Y') {
            printf("已取消操作。\n");
            continue;
        }

        ret = appointment_cancel(db, appoint_id, user->id);
        if (ret == 0) {
            printf("预约 %d 已取消。\n", appoint_id);
            list[i].status = 2;             /* 本地列表同步状态，避免重复取消 */
        } else if (ret == -6) {
            printf("取消失败：该预约已被医生接诊或已被取消\n");
        } else {
            printf("取消失败：%s\n", err_text(ret));
        }
    }

    appointment_list_free(list);
}

/* ==========================================================================
 * 3. 我的就诊记录
 * ========================================================================== */
static void do_my_visits(DB_HANDLE* db, const LoginUser* user) {
    VisitRecord* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = visit_query_by_patient(db, user->id, &list, &count);
    if (ret != 0) {
        printf("[查询失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 我的就诊记录（共 %d 条）===\n", count);
    if (count == 0) {
        printf("  还没有就诊记录。\n");
        visit_list_free(list);
        return;
    }

    for (i = 0; i < count; i++) {
        printf("----------------------------------------\n");
        printf("  就诊ID %d   医生 %s   时间 %s\n",
               list[i].visit_id, list[i].doctor_id,
               list[i].visit_time[0] ? list[i].visit_time : "未知");
        printf("  主诉     %s\n", list[i].chief_complaint[0] ? list[i].chief_complaint : "（未填）");
        printf("  现病史   %s\n", list[i].present_illness[0] ? list[i].present_illness : "（未填）");
        if (list[i].temp > 0)       printf("  体温     %.1f ℃\n", list[i].temp);
        printf("  血压     %s\n", list[i].blood_pressure[0] ? list[i].blood_pressure : "（未填）");
        if (list[i].heart_rate > 0) printf("  心率     %d 次/分\n", list[i].heart_rate);
    }
    printf("----------------------------------------\n");

    visit_list_free(list);
}

/* ==========================================================================
 * 4. 我的处方（可看明细）
 * ========================================================================== */
static void do_my_prescriptions(DB_HANDLE* db, const LoginUser* user) {
    Prescription* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = prescription_query_by_patient(db, user->id, &list, &count);
    if (ret != 0) {
        printf("[查询失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 我的处方（共 %d 张）===\n", count);
    if (count == 0) {
        printf("  还没有处方记录。\n");
        prescription_list_free(list);
        return;
    }

    {
        const int   w[] = {-8, -8, 8, 10, 22, 0};
        const char* cells[6];
        char        presc_txt[16];
        char        visit_txt[16];

        cells[0] = "处方ID"; cells[1] = "就诊ID"; cells[2] = "状态";
        cells[3] = "开方医生"; cells[4] = "开方时间"; cells[5] = "诊断";
        table_row(cells, w, 6);

        for (i = 0; i < count; i++) {
            snprintf(presc_txt, sizeof(presc_txt), "%d", list[i].presc_id);
            snprintf(visit_txt, sizeof(visit_txt), "%d", list[i].visit_id);
            cells[0] = presc_txt;
            cells[1] = visit_txt;
            cells[2] = presc_status_text(list[i].status);
            cells[3] = list[i].doctor_id;
            cells[4] = list[i].create_time[0] ? list[i].create_time : "未知";
            cells[5] = list[i].diagnosis[0] ? list[i].diagnosis : "（未填）";
            table_row(cells, w, 6);
        }
    }

    /* 输入处方ID看明细 */
    for (;;) {
        int presc_id = 0;

        prompt_int("\n输入处方ID查看明细（0 结束）：", &presc_id);
        if (presc_id <= 0) break;

        for (i = 0; i < count; i++) {
            if (list[i].presc_id == presc_id) break;
        }
        if (i >= count) {
            printf("处方ID %d 不在你的处方列表里，请核对。\n", presc_id);
            continue;
        }

        /* 查出来的这张处方就是列表里本人的，直接用 ID 拉明细 */
        {
            Prescription p;
            PrescDetail* items = NULL;
            int item_count = 0;
            double total = 0.0;
            int j;

            ret = prescription_query_by_id(db, presc_id, &p, &items, &item_count);
            if (ret != 0) {
                printf("[读取处方明细失败：%s]\n", err_text(ret));
                prescription_detail_free(items);
                continue;
            }

            printf("----------------------------------------\n");
            printf("  处方ID %d   状态 %s   开方时间 %s\n",
                   p.presc_id, presc_status_text(p.status),
                   p.create_time[0] ? p.create_time : "未知");
            printf("  诊断：%s\n", p.diagnosis[0] ? p.diagnosis : "（未填）");
            if (item_count == 0) {
                printf("  （没有明细）\n");
            } else {
                const int   w[] = {12, -6, -10, -12, 0};
                const char* cells[5];
                char        qty_txt[16];
                char        price_txt[16];
                char        sub_txt[16];

                cells[0] = "药品编号"; cells[1] = "数量"; cells[2] = "单价";
                cells[3] = "小计";     cells[4] = "用法";
                table_row(cells, w, 5);

                for (j = 0; j < item_count; j++) {
                    snprintf(qty_txt,   sizeof(qty_txt),   "%d",    items[j].quantity);
                    snprintf(price_txt, sizeof(price_txt), "%.2f",  items[j].unit_price);
                    snprintf(sub_txt,   sizeof(sub_txt),   "%.2f",  items[j].subtotal);
                    cells[0] = items[j].med_id;
                    cells[1] = qty_txt;
                    cells[2] = price_txt;
                    cells[3] = sub_txt;
                    cells[4] = items[j].dosage[0] ? items[j].dosage : "未填";
                    table_row(cells, w, 5);
                    total += items[j].subtotal;
                }
                printf("  处方总金额：%.2f 元\n", total);
            }
            printf("----------------------------------------\n");
            prescription_detail_free(items);
        }
    }

    prescription_list_free(list);
}

/* ==========================================================================
 * 5. 我的病历
 * ========================================================================== */
static void do_my_records(DB_HANDLE* db, const LoginUser* user) {
    MedicalRecord* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = medical_record_query_by_patient(db, user->id, &list, &count);
    if (ret != 0) {
        printf("[查询失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 我的病历（共 %d 条）===\n", count);
    if (count == 0) {
        printf("  还没有病历记录。\n");
        medical_record_list_free(list);
        return;
    }

    for (i = 0; i < count; i++) {
        printf("----------------------------------------\n");
        printf("  病历ID %d   就诊ID %d   医生 %s\n",
               list[i].record_id, list[i].visit_id, list[i].doctor_id);
        printf("  最后更新 %s\n", list[i].update_time[0] ? list[i].update_time : "未知");
        printf("  过敏史   %s\n", list[i].allergy_history[0] ? list[i].allergy_history : "无");
        printf("  既往史   %s\n", list[i].past_history[0] ? list[i].past_history : "（未填）");
        printf("  家族史   %s\n", list[i].family_history[0] ? list[i].family_history : "（未填）");
    }
    printf("----------------------------------------\n");

    medical_record_list_free(list);
}

/* ==========================================================================
 * 6. 修改个人资料 / 密码
 * ========================================================================== */
static void do_profile(DB_HANDLE* db, const LoginUser* user) {
    Patient p;
    char    phone[sizeof(p.phone)];
    char    address[sizeof(p.address)];
    char    old_pwd[64];
    char    new_pwd[64];
    char    confirm[64];
    int     ret;

    printf("\n--- 修改个人资料 ---\n");
    printf("  1. 修改联系方式（电话/住址）\n");
    printf("  2. 修改登录密码\n");
    printf("  0. 返回\n");
    printf("请输入选项[0-2]：");

    {
        int choice = 0;
        int r = read_choice(&choice);
        if (r != 0) { printf("输入无效，已返回。\n"); return; }

        if (choice == 0) return;

        if (choice == 1) {
            /* 先查出当前资料，直接回车保留原值 */
            ret = patient_query_by_id(db, user->id, &p);
            if (ret != 0) {
                printf("[读取个人资料失败：%s]\n", err_text(ret));
                return;
            }
            printf("当前电话：%s\n", p.phone[0] ? p.phone : "（未填）");
            printf("当前住址：%s\n", p.address[0] ? p.address : "（未填）");
            printf("直接回车即保留原值。\n");

            prompt("新电话：", phone, sizeof(phone));
            prompt("新住址：", address, sizeof(address));
            if (phone[0] == '\0')   snprintf(phone,   sizeof(phone),   "%s", p.phone);
            if (address[0] == '\0') snprintf(address, sizeof(address), "%s", p.address);

            ret = patient_update_self(db, user->id, phone, address);
            if (ret == 0) {
                printf("联系方式已更新。\n");
            } else {
                printf("修改失败：%s\n", err_text(ret));
            }
            return;
        }

        if (choice == 2) {
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
            return;
        }

        printf("无效选项\n");
    }
}

/* ==========================================================================
 * 患者工作台主循环
 * ========================================================================== */
void menu_patient(DB_HANDLE* db, const LoginUser* user) {
    int running = 1;

    /* 角色 3 = 患者，越权直接拒绝 */
    if (!db || !user || user->role != 3) return;

    printf("\n========================================\n");
    printf("  患者工作台  %s(%s)\n", user->name, user->id);
    printf("========================================\n");

    while (running) {
        int choice;
        int r;

        printf("\n========================================\n");
        printf("  1. 挂号\n");
        printf("  2. 我的预约\n");
        printf("  3. 我的就诊记录\n");
        printf("  4. 我的处方\n");
        printf("  5. 我的病历\n");
        printf("  6. 修改个人资料/密码\n");
        printf("  0. 返回登录\n");
        printf("========================================\n");
        printf("请输入选项[0-6]：");
        fflush(stdout);

        r = read_choice(&choice);
        if (r == -2) return;                     /* 输入结束，直接返回，避免空转 */
        if (r != 0) { printf("输入无效，请重新输入\n"); continue; }

        switch (choice) {
            case 1: do_appoint(db, user);          break;
            case 2: do_my_appointments(db, user);  break;
            case 3: do_my_visits(db, user);        break;
            case 4: do_my_prescriptions(db, user); break;
            case 5: do_my_records(db, user);       break;
            case 6: do_profile(db, user);          break;
            case 0: running = 0;                   break;
            default: printf("无效选项\n");         break;
        }
    }

    printf("\n已退出患者工作台。\n");
}
