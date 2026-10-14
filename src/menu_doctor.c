/* ==========================================================================
 * menu_doctor.c —— 医生工作台 实现
 * --------------------------------------------------------------------------
 * 负责组：医生组
 * 作者：陈翔宇
 * 创建日期：2026-10-13
 * 对应头文件：inc/menu.h
 * 对应界面原型：教程 1.7 "医生工作台"
 *
 * 菜单项和各自调用的函数（与 menu.h 里 menu_doctor 的注释、教程 1.7 原型一致）：
 *   进入时    列出当日待就诊预约 -> appointment_query_by_doctor_today
 *   1. 接诊                      -> visit_add
 *                                  （录完主诉和体检数据后，按教程 1.3 的业务流程图
 *                                    接着引导开处方、写病历）
 *   2. 我的就诊记录              -> visit_query_by_doctor
 *   3. 开处方                    -> 二级选择：
 *                                    1 新开处方 -> medicine_query_all 选药、
 *                                                  prescription_add 开方
 *                                    2 查看/作废已有处方 -> prescription_query_by_patient、
 *                                                  prescription_query_by_id、prescription_void
 *   4. 书写病历                  -> medical_record_add / medical_record_update
 *   5. 查询药品库存              -> medicine_query_all
 *   6. 修改密码                  -> user_change_password
 *   0. 返回登录
 *
 * 说明：
 *   - 主菜单严格保持教程 1.7 的 6 项 + 0 返回，没有增删。
 *     M05 要求"处方可作废"，但 6 项里没有位置，所以作废放在第 3 项里面
 *     （新开 / 查看并作废 二级选择），这样原型和功能都保住。
 *   - 输入一律按 main.c 里 read_line 的写法读整行，不用 scanf("%d")，
 *     避免 scanf 遇到字母卡住、以及残留字符窜到下一次输入里。
 *   - 所有失败分支都按各模块注释里约定的返回码给出中文提示。
 *   - 处方明细最多 64 条，和 prescription.c 里的容量限制保持一致。
 * ========================================================================== */

#include "menu.h"
#include "visit.h"
#include "prescription.h"
#include "medical_record.h"
#include "medicine.h"
#include "appointment.h"
#include "doctor_deps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 处方一次最多开多少味药，和 prescription.c 里的 PRESC_MAX_ITEMS 对齐 */
#define MENU_MAX_ITEMS 64

/* 各类输入缓冲区。中文一个字在 UTF-8 里占 3 字节，这里按字节留足余量 */
#define MENU_MID_BUF 256

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

/* 显示提示语后读一整行字符串 */
static void prompt(const char* label, char* buf, int size) {
    printf("%s", label);
    fflush(stdout);
    if (read_line(buf, size) != 0) buf[0] = '\0';
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

/* 输入为空时用旧值兜底（病历修改里"直接回车保留原值"） */
static void keep_or(const char* defval, char* buf, int size) {
    if (buf[0] == '\0' && defval && defval[0]) {
        snprintf(buf, (size_t)size, "%s", defval);
    }
}

/* 把 src 拷进定长缓冲区，保证以 '\0' 结尾。
 *
 * 这里不用 snprintf：当目标缓冲区比源数组小时，gcc 会报
 * -Wformat-truncation。医生编号（D001）和登录用户编号都不会超过几位，
 * 实际不会截断，用这个函数把"已确保结尾"这件事一次说清楚。
 * 直接把结构体成员赋给同类型的定长字段（如 doctor_id = user->id）
 * 也可以，编译器按数组赋值处理，不会警告，只是这里显式收口更好读。 */
static void copy_str(char* dst, size_t dstsz, const char* src) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

/* 把 "上午\n" 之类整理成紧凑的一行：换行/制表符换成空格，连续空格合成一个 */
static void flatten(char* s) {
    int i;
    int j = 0;
    int last_space = 0;
    for (i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ') {
            if (last_space) continue;       /* 连续空格只留一个 */
            last_space = 1;
        } else {
            last_space = 0;
        }
        s[j++] = c;
    }
    s[j] = '\0';
}

/* 把文本整理成一行，空则返回占位串。
 *
 * out 的大小必须和 src 所在字段一致（调用处传 sizeof(list[i].symptom)）。
 * 两个用途：1) 去掉末尾可能带的换行，只填一次就不会连打两遍；
 *          2) 空的时候直接给出"未填 XX"，省掉每个调用点自己判空。 */
static const char* one_line_placeholder(const char* src, char* out, size_t outsz,
                                        const char* placeholder) {
    copy_str(out, outsz, src);
    flatten(out);
    return out[0] ? out : placeholder;
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

/* 处方状态：t_prescription.status 1 有效 / 0 作废 */
static const char* presc_status_text(int status) {
    return status == 1 ? "有效" : "已作废";
}

/* ==========================================================================
 * 展示辅助
 * ========================================================================== */

/* 拉取并打印当日待就诊预约，进入工作台时调用 */
static int show_today(DB_HANDLE* db, const char* doctor_id) {
    Appointment* list = NULL;
    int count = 0;
    int i;

    int ret = appointment_query_by_doctor_today(db, doctor_id, &list, &count);
    if (ret != 0) {
        printf("\n[无法读取当日待就诊列表：%s]\n", err_text(ret));
        return ret;
    }

    printf("\n----------------------------------------\n");
    printf("今日待就诊预约：\n");
    if (count == 0) {
        printf("  （今天没有待就诊的预约）\n");
    } else {
        for (i = 0; i < count; i++) {
            char slot[sizeof(list[i].time_slot)];
            char symptom[sizeof(list[i].symptom)];

            printf("  [%d] %-6s %-12s %s\n",
                   list[i].appoint_id,
                   one_line_placeholder(list[i].time_slot, slot, sizeof(slot), "未填时段"),
                   list[i].patient_id,
                   one_line_placeholder(list[i].symptom, symptom, sizeof(symptom), "未填症状"));
        }
    }
    printf("----------------------------------------\n");

    appointment_list_free(list);
    return 0;
}

/* 列出库存，供医生选药。库存低于 50 的标"库存不足" */
static void do_medicine_list(DB_HANDLE* db) {
    Medicine* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = medicine_query_all(db, &list, &count);
    if (ret != 0) {
        printf("[查询药品失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 药品库存（共 %d 种）===\n", count);
    if (count == 0) {
        printf("  药品目录为空，请联系管理员先维护药品。\n");
        medicine_list_free(list);
        return;
    }

    printf("  %-10s %-16s %-14s %6s %8s %7s  %s\n",
           "药品编号", "名称", "规格", "单位", "单价", "库存", "厂家");
    for (i = 0; i < count; i++) {
        printf("  %-10s %-16s %-14s %6s %8.2f %7d  %s%s\n",
               list[i].med_id, list[i].med_name, list[i].spec, list[i].unit,
               list[i].price, list[i].stock,
               list[i].manufacturer,
               list[i].stock < 50 ? "  [库存不足]" : "");
    }

    medicine_list_free(list);
}

/* 打印一条就诊记录的完整内容 */
static void print_visit_detail(const VisitRecord* v) {
    printf("  就诊ID %d   预约ID %d   患者 %s\n",
           v->visit_id, v->appoint_id, v->patient_id);
    printf("  就诊时间 %s\n", v->visit_time[0] ? v->visit_time : "未知");
    printf("  主诉     %s\n", v->chief_complaint[0] ? v->chief_complaint : "（未填）");
    printf("  现病史   %s\n", v->present_illness[0] ? v->present_illness : "（未填）");
    if (v->temp > 0)       printf("  体温     %.1f ℃\n", v->temp);
    else                   printf("  体温     （未填）\n");
    printf("  血压     %s\n", v->blood_pressure[0] ? v->blood_pressure : "（未填）");
    if (v->heart_rate > 0) printf("  心率     %d 次/分\n", v->heart_rate);
    else                   printf("  心率     （未填）\n");
}

/* ==========================================================================
 * 病历：一个就诊对应一条病历，已存在就改、不存在就新增
 * ========================================================================== */
static void write_record(DB_HANDLE* db, const LoginUser* user,
                         int visit_id, const char* patient_id) {
    MedicalRecord r;
    /* 这三块缓冲区的大小和结构体字段严格一致，既不会截断也不会触发
     * 编译器的 -Wformat-truncation 警告（缓冲区比源串小就会被警告） */
    char allergy[sizeof(r.allergy_history)];
    char past[sizeof(r.past_history)];
    char family[sizeof(r.family_history)];
    int found;
    int ret;

    if (visit_id <= 0) return;

    memset(&r, 0, sizeof(r));
    found = medical_record_find_by_visit(db, visit_id, &r);
    if (found < 0) {
        printf("[读取病历失败：%s]\n", err_text(found));
        return;
    }

    /* 接口只按记录里的字段定位，这里补齐患者、医生和就诊 ID
     * （doctor_id 定长数组，用同类型的赋值，编译器不会警告） */
    copy_str(r.patient_id, sizeof(r.patient_id), patient_id);
    memcpy(r.doctor_id, user->id, sizeof(r.doctor_id));
    r.doctor_id[sizeof(r.doctor_id) - 1] = '\0';
    r.visit_id = visit_id;

    if (found == 1) {
        printf("\n本次就诊已有病历（病历ID %d，最后更新 %s）\n",
               r.record_id, r.update_time[0] ? r.update_time : "未知");
        printf("当前内容：过敏史[%s] 既往史[%s] 家族史[%s]\n",
               r.allergy_history, r.past_history, r.family_history);
        printf("直接回车即保留原值。\n");
    } else {
        printf("\n为本次就诊书写病历：\n");
    }

    snprintf(allergy, sizeof(allergy), "%s", r.allergy_history);
    snprintf(past,    sizeof(past),    "%s", r.past_history);
    snprintf(family,  sizeof(family),  "%s", r.family_history);

    /* 过敏史是必填项：确实没有过敏就直接回车，按空字符串（无过敏）保存 */
    prompt("过敏史（没有过敏就直接回车）：", allergy, sizeof(allergy));
    prompt("既往史（没有就直接回车）：",     past,    sizeof(past));
    prompt("家族史（没有就直接回车）：",     family,  sizeof(family));

    keep_or(r.allergy_history, allergy, sizeof(allergy));
    keep_or(r.past_history,    past,    sizeof(past));
    keep_or(r.family_history,  family,  sizeof(family));

    if (allergy[0] == '\0') {
        printf("（过敏史为空，按\"无过敏\"保存）\n");
    }

    snprintf(r.allergy_history, sizeof(r.allergy_history), "%s", allergy);
    snprintf(r.past_history,    sizeof(r.past_history),    "%s", past);
    snprintf(r.family_history,  sizeof(r.family_history),  "%s", family);

    if (found == 1) {
        ret = medical_record_update(db, &r);
        if (ret == 0) {
            printf("病历已更新（病历ID %d）\n", r.record_id);
        } else if (ret == -7) {
            printf("修改失败：病历已超过 24 小时，已锁定不能改\n");
        } else if (ret == -6) {
            printf("修改失败：只能修改本人书写的病历\n");
        } else {
            printf("修改失败：%s\n", err_text(ret));
        }
    } else {
        ret = medical_record_add(db, &r);
        if (ret == 0) {
            printf("病历已保存（病历ID %d）\n", r.record_id);
        } else {
            printf("保存失败：%s\n", err_text(ret));
        }
    }
}

/* ==========================================================================
 * 处方：查看明细 / 作废
 * ========================================================================== */

/* 打印一张处方的明细和总金额 */
static void print_presc_detail(DB_HANDLE* db, int presc_id) {
    Prescription p;
    PrescDetail* items = NULL;
    int item_count = 0;
    int ret;
    int i;
    double total = 0.0;

    ret = prescription_query_by_id(db, presc_id, &p, &items, &item_count);
    if (ret != 0) {
        printf("  [读取处方明细失败：%s]\n", err_text(ret));
        prescription_detail_free(items);   /* 失败时也要放掉，免得漏内存 */
        return;
    }

    printf("  处方ID %d   就诊ID %d   患者 %s   医生 %s\n",
           p.presc_id, p.visit_id, p.patient_id, p.doctor_id);
    printf("  状态 %s   开方时间 %s\n",
           presc_status_text(p.status),
           p.create_time[0] ? p.create_time : "未知");
    printf("  诊断：%s\n", p.diagnosis[0] ? p.diagnosis : "（未填）");

    if (item_count == 0) {
        printf("  （没有明细）\n");
    } else {
        printf("  %-12s %6s %10s %12s  用法\n", "药品编号", "数量", "单价", "小计");
        for (i = 0; i < item_count; i++) {
            printf("  %-12s %6d %10.2f %12.2f  %s\n",
                   items[i].med_id, items[i].quantity,
                   items[i].unit_price, items[i].subtotal,
                   items[i].dosage[0] ? items[i].dosage : "未填");
            total += items[i].subtotal;
        }
        printf("  处方总金额：%.2f 元\n", total);
    }

    prescription_detail_free(items);
}

/* 查看某个患者的处方，可看作废。医生只能作废本人开的处方。
 * 参数：known_patient_id 从接诊流程过来时已知患者，直接用它；否则问医生要 */
static void prescription_review(DB_HANDLE* db, const LoginUser* user,
                               const char* known_patient_id) {
    char patient_id[20];
    char ans[16];
    Prescription* list = NULL;
    int count = 0;
    int ret;
    int i;

    if (known_patient_id && known_patient_id[0]) {
        snprintf(patient_id, sizeof(patient_id), "%s", known_patient_id);
    } else {
        prompt("请输入患者编号（如 P20260001，0 返回）：", patient_id, sizeof(patient_id));
        if (patient_id[0] == '\0' || strcmp(patient_id, "0") == 0) return;
    }

    ret = prescription_query_by_patient(db, patient_id, &list, &count);
    if (ret != 0) {
        printf("[查询失败：%s]\n", err_text(ret));
        return;
    }

    printf("\n=== 患者 %s 的处方（共 %d 张）===\n", patient_id, count);
    if (count == 0) {
        printf("  该患者还没有处方。\n");
        prescription_list_free(list);
        return;
    }

    printf("  %-8s %-8s %-8s %-10s %s\n", "处方ID", "就诊ID", "状态", "开方医生", "诊断");
    for (i = 0; i < count; i++) {
        printf("  %-8d %-8d %-8s %-10s %s\n",
               list[i].presc_id, list[i].visit_id,
               presc_status_text(list[i].status), list[i].doctor_id,
               list[i].diagnosis[0] ? list[i].diagnosis : "（未填）");
    }

    /* 明细和作废都按处方 ID 操作，这里循环处理，避免医生反复进退菜单 */
    for (;;) {
        int presc_id = 0;
        int found = 0;

        prompt_int("\n输入处方ID查看明细并可作废（0 结束）：", &presc_id);
        if (presc_id <= 0) break;

        for (i = 0; i < count; i++) {
            if (list[i].presc_id == presc_id) { found = 1; break; }
        }
        if (!found) {
            printf("处方ID %d 不在上面的列表里，请核对。\n", presc_id);
            continue;
        }

        printf("----------------------------------------\n");
        print_presc_detail(db, presc_id);
        printf("----------------------------------------\n");

        if (list[i].status != 1) {
            printf("该处方已作废，不能重复作废。\n");
            continue;
        }
        if (strcmp(list[i].doctor_id, user->id) != 0) {
            printf("该处方是 %s 开的，只有本人能作废。\n", list[i].doctor_id);
            continue;
        }

        prompt("确认作废这张处方？(y/N)：", ans, sizeof(ans));
        if (ans[0] != 'y' && ans[0] != 'Y') {
            printf("已取消。\n");
            continue;
        }

        ret = prescription_void(db, presc_id, user->id);
        if (ret == 0) {
            printf("处方 %d 已作废（注意：作废不回滚库存，药已发给患者）。\n", presc_id);
            list[i].status = 0;             /* 本地列表同步状态，避免重复作废 */
        } else {
            printf("作废失败：%s\n", err_text(ret));
        }
    }

    prescription_list_free(list);
}

/* ==========================================================================
 * 新开一张处方
 * 参数：known_visit_id / known_patient_id  —  刚从接诊流程过来时直接带上，
 *       否则传 0 / NULL，由医生自己填。
 * ========================================================================== */
static void prescription_new(DB_HANDLE* db, const LoginUser* user,
                             int known_visit_id, const char* known_patient_id) {
    Prescription p;
    PrescDetail items[MENU_MAX_ITEMS];
    int item_count = 0;
    int ret;
    int i;

    memset(&p, 0, sizeof(p));
    memset(items, 0, sizeof(items));

    /* 开方前先让医生看到库存 */
    do_medicine_list(db);

    p.visit_id = known_visit_id;
    if (p.visit_id <= 0) {
        prompt_int("请输入就诊ID（必须是自己已接诊的就诊记录，0 取消）：", &p.visit_id);
        if (p.visit_id <= 0) {
            printf("已取消开处方。\n");
            return;
        }
    } else {
        printf("\n=== 开处方（就诊ID %d）===\n", p.visit_id);
    }

    prompt("诊断结果：", p.diagnosis, sizeof(p.diagnosis));

    if (known_patient_id && known_patient_id[0]) {
        snprintf(p.patient_id, sizeof(p.patient_id), "%s", known_patient_id);
    } else {
        prompt("患者编号（如 P20260001）：", p.patient_id, sizeof(p.patient_id));
        if (p.patient_id[0] == '\0') {
            printf("患者编号不能为空，已取消开处方。\n");
            return;
        }
    }
    copy_str(p.doctor_id, sizeof(p.doctor_id), user->id);
    p.status = 1;

    printf("\n请逐味添加药品，药品编号留空即结束。处方最多 %d 味。\n", MENU_MAX_ITEMS);
    while (item_count < MENU_MAX_ITEMS) {
        /* 这两个缓冲区的大小和明细结构体字段一致，拷进去不会截断 */
        char med_id[sizeof(items[0].med_id)];
        char dosage[sizeof(items[0].dosage)];
        Medicine med;
        int qty;
        int med_ret;

        printf("\n--- 第 %d 味药 ---\n", item_count + 1);
        prompt("药品编号（直接回车结束）：", med_id, sizeof(med_id));
        if (med_id[0] == '\0') break;

        /* 先查有没有这个药品、库存够不够，让医生当场看到，不必等提交才报错 */
        memset(&med, 0, sizeof(med));
        med_ret = medicine_query_by_id(db, med_id, &med);
        if (med_ret == -4) {
            printf("没有编号为 %s 的药品，请重新输入。\n", med_id);
            continue;
        } else if (med_ret != 0) {
            printf("查询药品失败：%s\n", err_text(med_ret));
            continue;
        }

        prompt_int("数量：", &qty);
        if (qty <= 0) {
            printf("数量必须大于 0，本味药不加入。\n");
            continue;
        }
        if (qty > med.stock) {
            printf("库存不足：%s 当前库存 %d，本次要 %d。本味药不加入。\n",
                   med.med_name, med.stock, qty);
            continue;
        }

        prompt("用法用量（如 每日3次，每次1粒）：", dosage, sizeof(dosage));

        copy_str(items[item_count].med_id, sizeof(items[item_count].med_id), med_id);
        copy_str(items[item_count].dosage, sizeof(items[item_count].dosage), dosage);
        items[item_count].quantity = qty;
        item_count++;

        printf("已加入：%s（%s）%d %s\n", med.med_name, med.spec, qty, med.unit);
    }

    if (item_count == 0) {
        printf("没有添加任何药品，已取消开处方。\n");
        return;
    }

    printf("\n本次处方共 %d 味药，提交中……\n", item_count);
    ret = prescription_add(db, &p, items, item_count);

    if (ret == 0) {
        double total = 0.0;
        printf("\n开方成功，处方ID %d\n", p.presc_id);
        printf("  %-12s %6s %10s %12s  用法\n", "药品编号", "数量", "单价", "小计");
        for (i = 0; i < item_count; i++) {
            printf("  %-12s %6d %10.2f %12.2f  %s\n",
                   items[i].med_id, items[i].quantity,
                   items[i].unit_price, items[i].subtotal,
                   items[i].dosage[0] ? items[i].dosage : "未填");
            total += items[i].subtotal;
        }
        printf("处方总金额：%.2f 元\n", total);
        printf("对应药品库存已扣减。\n");
    } else if (ret == -5) {
        printf("\n开方失败：药品库存不足或药品不存在，整张处方已回滚，库存未变动。\n");
    } else if (ret == -4) {
        printf("\n开方失败：就诊记录 %d 不存在\n", p.visit_id);
    } else {
        printf("\n开方失败：%s（已回滚）\n", err_text(ret));
    }
}

/* ==========================================================================
 * 主菜单第 3 项「开处方」的入口
 * --------------------------------------------------------------------------
 * 教程 1.7 的医生工作台只有 6 项，没有单独给处方作废留位置，所以作废放在
 * 这一项里面：先让医生选是"新开处方"还是"查看/作废已有处方"。
 * 接诊流程走完时直接调 prescription_new()，不再多问一层。
 * ========================================================================== */
static void prescription_menu(DB_HANDLE* db, const LoginUser* user) {
    int choice = 0;
    int r;

    printf("\n--- 开处方 ---\n");
    printf("  1. 新开处方\n");
    printf("  2. 查看/作废已有处方\n");
    printf("  0. 返回\n");
    printf("请输入选项[0-2]：");
    fflush(stdout);

    r = read_choice(&choice);
    if (r != 0) { printf("输入无效，已返回。\n"); return; }

    switch (choice) {
        case 1: prescription_new(db, user, 0, NULL);  break;
        case 2: prescription_review(db, user, NULL);  break;
        case 0: break;
        default: printf("无效选项\n");                break;
    }
}

/* ==========================================================================
 * 1. 接诊
 * ========================================================================== */
static void do_visit(DB_HANDLE* db, const LoginUser* user) {
    Appointment* list = NULL;
    int count = 0;
    int ret;
    int i;
    int found = 0;
    VisitRecord v;

    /* 重新拉一次列表，保证看到的是最新的待就诊情况 */
    ret = appointment_query_by_doctor_today(db, user->id, &list, &count);
    if (ret != 0) {
        printf("[无法读取当日待就诊列表：%s]\n", err_text(ret));
        return;
    }
    if (count == 0) {
        printf("今天没有待就诊的预约，无法接诊。\n");
        appointment_list_free(list);
        return;
    }

    printf("\n--- 待就诊预约 ---\n");
    for (i = 0; i < count; i++) {
        char slot[sizeof(list[i].time_slot)];
        char symptom[sizeof(list[i].symptom)];

        printf("  [%d] %-6s %-12s %s\n",
               list[i].appoint_id,
               one_line_placeholder(list[i].time_slot, slot, sizeof(slot), "未填时段"),
               list[i].patient_id,
               one_line_placeholder(list[i].symptom, symptom, sizeof(symptom), "未填症状"));
    }

    memset(&v, 0, sizeof(v));
    prompt_int("请输入要接诊的预约编号（0 取消）：", &v.appoint_id);
    if (v.appoint_id <= 0) {
        printf("已取消接诊。\n");
        appointment_list_free(list);
        return;
    }

    /* 在这份列表里找这条预约，顺便把患者编号和症状带出来，医生不用重复输入 */
    for (i = 0; i < count; i++) {
        if (list[i].appoint_id == v.appoint_id) {
            found = 1;
            snprintf(v.patient_id, sizeof(v.patient_id), "%s", list[i].patient_id);
            snprintf(v.chief_complaint, sizeof(v.chief_complaint), "%s", list[i].symptom);
            break;
        }
    }
    appointment_list_free(list);

    if (!found) {
        printf("预约编号 %d 不在今天的待就诊列表里，请核对后重试。\n", v.appoint_id);
        return;
    }

    copy_str(v.doctor_id, sizeof(v.doctor_id), user->id);

    printf("\n=== 接诊：患者 %s，预约 %d ===\n", v.patient_id, v.appoint_id);
    printf("（主诉默认取患者挂号时填的症状，可直接回车沿用）\n");
    prompt("主诉：",            v.chief_complaint, sizeof(v.chief_complaint));
    prompt("现病史：",          v.present_illness, sizeof(v.present_illness));
    prompt_double("体温(℃)：",  &v.temp);
    prompt("血压(如 120/80)：", v.blood_pressure,  sizeof(v.blood_pressure));
    prompt_int("心率(次/分)：", &v.heart_rate);

    /* 兜一下明显不合理的输入，避免把 0 或者离谱的值写进库 */
    if (v.temp != 0.0 && (v.temp < 30.0 || v.temp > 45.0)) {
        printf("提示：体温 %.1f 不在 30.0~45.0 之间，按未填保存\n", v.temp);
        v.temp = 0.0;
    }
    if (v.heart_rate < 0 || v.heart_rate > 300) {
        printf("提示：心率 %d 不合理，按未填保存\n", v.heart_rate);
        v.heart_rate = 0;
    }

    ret = visit_add(db, &v);
    if (ret == 0) {
        char ans[16];

        printf("\n接诊成功，就诊ID %d，就诊时间 %s\n",
               v.visit_id, v.visit_time[0] ? v.visit_time : "已记录");
        printf("该预约状态已置为\"已就诊\"。\n");

        /* 接诊后按教程 1.3 的业务流程图继续走：开处方、写病历 */
        prompt("\n是否现在为该患者开处方？(y/N)：", ans, sizeof(ans));
        if (ans[0] == 'y' || ans[0] == 'Y') {
            prescription_new(db, user, v.visit_id, v.patient_id);
        }

        prompt("是否现在书写病历？(y/N)：", ans, sizeof(ans));
        if (ans[0] == 'y' || ans[0] == 'Y') {
            write_record(db, user, v.visit_id, v.patient_id);
        }
    } else if (ret == -4) {
        printf("\n接诊失败：预约不存在\n");
    } else if (ret == -6) {
        printf("\n接诊失败：该预约不属于你，或已经被接诊/已取消\n");
    } else {
        printf("\n接诊失败：%s（已回滚，未写入任何数据）\n", err_text(ret));
    }
}

/* ==========================================================================
 * 2. 我的就诊记录
 * ========================================================================== */
static void do_my_visits(DB_HANDLE* db, const LoginUser* user) {
    VisitRecord* list = NULL;
    int count = 0;
    int ret;
    int i;

    ret = visit_query_by_doctor(db, user->id, &list, &count);
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
        print_visit_detail(&list[i]);
    }
    printf("----------------------------------------\n");

    visit_list_free(list);
}

/* ==========================================================================
 * 4. 书写病历（也可修改本人 24 小时内写过的病历）
 * ========================================================================== */
static void do_record(DB_HANDLE* db, const LoginUser* user) {
    VisitRecord* list = NULL;
    int count = 0;
    int i;
    int visit_id = 0;
    char patient_id[20] = "";

    prompt_int("请输入就诊ID（0 返回）：", &visit_id);
    if (visit_id <= 0) return;

    /* 患者编号由这条就诊记录决定，先查出来，医生不用手输 */
    if (visit_query_by_doctor(db, user->id, &list, &count) == 0) {
        for (i = 0; i < count; i++) {
            if (list[i].visit_id == visit_id) {
                snprintf(patient_id, sizeof(patient_id), "%s", list[i].patient_id);
                break;
            }
        }
        visit_list_free(list);
    }

    if (patient_id[0] == '\0') {
        printf("没有找到就诊ID %d，或这条就诊记录不是你的，请核对后重试。\n", visit_id);
        return;
    }

    write_record(db, user, visit_id, patient_id);
}

/* ==========================================================================
 * 6. 修改密码
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
 * 医生工作台主循环
 * ========================================================================== */
void menu_doctor(DB_HANDLE* db, const LoginUser* user) {
    int running = 1;

    /* 角色 2 = 医生，越权直接拒绝（教程 1.6 安全需求：越权操作直接拒绝） */
    if (!db || !user || user->role != 2) return;

    printf("\n========================================\n");
    printf("  医生工作台  %s(%s)\n", user->name, user->id);
    printf("========================================\n");

    /* 进入工作台先拉一次当日待就诊列表（教程 1.7 界面原型） */
    show_today(db, user->id);

    while (running) {
        int choice;
        int r;

        printf("\n========================================\n");
        printf("  1. 接诊(输入预约编号)\n");
        printf("  2. 我的就诊记录\n");
        printf("  3. 开处方\n");
        printf("  4. 书写病历\n");
        printf("  5. 查询药品库存\n");
        printf("  6. 修改密码\n");
        printf("  0. 返回登录\n");
        printf("========================================\n");
        printf("请输入选项[0-6]：");
        fflush(stdout);

        r = read_choice(&choice);
        if (r == -2) return;                     /* 输入结束，直接返回，避免空转 */
        if (r != 0) { printf("输入无效，请重新输入\n"); continue; }

        switch (choice) {
            case 1: do_visit(db, user);           break;
            case 2: do_my_visits(db, user);       break;
            case 3: prescription_menu(db, user);  break;
            case 4: do_record(db, user);          break;
            case 5: do_medicine_list(db);         break;
            case 6: do_change_password(db, user); break;
            case 0: running = 0;                  break;
            default: printf("无效选项\n");        break;
        }
    }

    printf("\n已退出医生工作台。\n");
}
