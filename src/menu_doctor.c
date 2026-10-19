
#include "menu.h"
#include "visit.h"
#include "prescription.h"
#include "medical_record.h"
#include "medicine.h"
#include "appointment.h"
#include "doctor_deps.h"
#include "utils.h"
#include "menu_common.h"
#include "cli_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MENU_MAX_ITEMS 64
#define MENU_MID_BUF 256














static void flatten(char* s) {
    int i;
    int j = 0;
    int last_space = 0;
    for (i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ') {
            if (last_space) continue;
            last_space = 1;
        } else {
            last_space = 0;
        }
        s[j++] = c;
    }
    s[j] = '\0';
}

static const char* one_line_placeholder(const char* src, char* out, size_t outsz,
                                        const char* placeholder) {
    copy_str(out, outsz, src);
    flatten(out);
    return out[0] ? out : placeholder;
}






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
            char        slot[sizeof(list[i].time_slot)];
            char        symptom[sizeof(list[i].symptom)];
            char        id_txt[16];
            const int   w[] = {8, 6, 12, 0};
            const char* cells[4];

            snprintf(id_txt, sizeof(id_txt), "[%d]", list[i].appoint_id);
            cells[0] = id_txt;
            cells[1] = one_line_placeholder(list[i].time_slot, slot, sizeof(slot), "未填时段");
            cells[2] = list[i].patient_id;
            cells[3] = one_line_placeholder(list[i].symptom, symptom, sizeof(symptom), "未填症状");
            table_row(cells, w, 4);
        }
    }
    printf("----------------------------------------\n");

    appointment_list_free(list);
    return 0;
}

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
            snprintf(stock_txt, sizeof(stock_txt), "%d",   list[i].stock);
            snprintf(man_txt,   sizeof(man_txt),   "%s%s", list[i].manufacturer,
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

static void write_record(DB_HANDLE* db, const LoginUser* user,
                         int visit_id, const char* patient_id) {
    MedicalRecord r;
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

    cli_prompt("过敏史（没有过敏就直接回车）：", allergy, sizeof(allergy));
    cli_prompt("既往史（没有就直接回车）：",     past,    sizeof(past));
    cli_prompt("家族史（没有就直接回车）：",     family,  sizeof(family));

    cli_keep_or(r.allergy_history, allergy, sizeof(allergy));
    cli_keep_or(r.past_history,    past,    sizeof(past));
    cli_keep_or(r.family_history,  family,  sizeof(family));

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


static void print_presc_items(const PrescDetail* items, int count) {
    const int   w[] = {12, -6, -10, -12, 0};
    const char* cells[5];
    char        qty_txt[16];
    char        price_txt[16];
    char        sub_txt[16];
    int         i;

    cells[0] = "药品编号"; cells[1] = "数量"; cells[2] = "单价";
    cells[3] = "小计";     cells[4] = "用法";
    table_row(cells, w, 5);

    for (i = 0; i < count; i++) {
        snprintf(qty_txt,   sizeof(qty_txt),   "%d",   items[i].quantity);
        snprintf(price_txt, sizeof(price_txt), "%.2f", items[i].unit_price);
        snprintf(sub_txt,   sizeof(sub_txt),   "%.2f", items[i].subtotal);
        cells[0] = items[i].med_id;
        cells[1] = qty_txt;
        cells[2] = price_txt;
        cells[3] = sub_txt;
        cells[4] = items[i].dosage[0] ? items[i].dosage : "未填";
        table_row(cells, w, 5);
    }
}

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
        prescription_detail_free(items);
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
        print_presc_items(items, item_count);
        for (i = 0; i < item_count; i++) total += items[i].subtotal;
        printf("  处方总金额：%.2f 元\n", total);
    }

    prescription_detail_free(items);
}

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
        cli_prompt("请输入患者编号（如 P20260001，0 返回）：", patient_id, sizeof(patient_id));
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

    {
        const int   w[] = {-8, -8, 8, 10, 0};
        const char* cells[5];
        char        presc_txt[16];
        char        visit_txt[16];

        cells[0] = "处方ID"; cells[1] = "就诊ID"; cells[2] = "状态";
        cells[3] = "开方医生"; cells[4] = "诊断";
        table_row(cells, w, 5);

        for (i = 0; i < count; i++) {
            snprintf(presc_txt, sizeof(presc_txt), "%d", list[i].presc_id);
            snprintf(visit_txt, sizeof(visit_txt), "%d", list[i].visit_id);
            cells[0] = presc_txt;
            cells[1] = visit_txt;
            cells[2] = presc_status_text(list[i].status);
            cells[3] = list[i].doctor_id;
            cells[4] = list[i].diagnosis[0] ? list[i].diagnosis : "（未填）";
            table_row(cells, w, 5);
        }
    }

    for (;;) {
        int presc_id = 0;
        int found = 0;

        cli_prompt_int("\n输入处方ID查看明细并可作废（0 结束）：", &presc_id);
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

        cli_prompt("确认作废这张处方？(y/N)：", ans, sizeof(ans));
        if (ans[0] != 'y' && ans[0] != 'Y') {
            printf("已取消。\n");
            continue;
        }

        ret = prescription_void(db, presc_id, user->id);
        if (ret == 0) {
            printf("处方 %d 已作废（注意：作废不回滚库存，药已发给患者）。\n", presc_id);
            list[i].status = 0;
        } else {
            printf("作废失败：%s\n", err_text(ret));
        }
    }

    prescription_list_free(list);
}

static int lookup_my_visit(DB_HANDLE* db, const char* doctor_id, int visit_id,
                           char* out_patient, size_t out_size) {
    VisitRecord* list = NULL;
    int count = 0;
    int found = 0;
    int i;

    if (!out_patient || out_size == 0) return 0;
    out_patient[0] = '\0';

    if (visit_query_by_doctor(db, doctor_id, &list, &count) != 0) return 0;

    for (i = 0; i < count; i++) {
        if (list[i].visit_id == visit_id) {
            snprintf(out_patient, out_size, "%s", list[i].patient_id);
            found = 1;
            break;
        }
    }
    visit_list_free(list);
    return found;
}

static void prescription_new(DB_HANDLE* db, const LoginUser* user,
                             int known_visit_id, const char* known_patient_id) {
    Prescription p;
    PrescDetail items[MENU_MAX_ITEMS];
    int item_count = 0;
    int ret;
    int i;

    memset(&p, 0, sizeof(p));
    memset(items, 0, sizeof(items));

    do_medicine_list(db);

    p.visit_id = known_visit_id;
    if (p.visit_id <= 0) {
        cli_prompt_int("请输入就诊ID（必须是自己已接诊的就诊记录，0 取消）：", &p.visit_id);
        if (p.visit_id <= 0) {
            printf("已取消开处方。\n");
            return;
        }
    } else {
        printf("\n=== 开处方（就诊ID %d）===\n", p.visit_id);
    }

    if (!lookup_my_visit(db, user->id, p.visit_id, p.patient_id, sizeof(p.patient_id))) {
        printf("就诊ID %d 不在你的就诊记录里，只能为自己接诊过的就诊开方。\n", p.visit_id);
        return;
    }
    if (known_patient_id && known_patient_id[0] &&
        strcmp(known_patient_id, p.patient_id) != 0) {
        printf("提示：该就诊记录里的患者是 %s，已按记录开方。\n", p.patient_id);
    }
    printf("患者编号：%s\n", p.patient_id);

    cli_prompt("诊断结果：", p.diagnosis, sizeof(p.diagnosis));

    copy_str(p.doctor_id, sizeof(p.doctor_id), user->id);
    p.status = 1;

    printf("\n请逐味添加药品，药品编号留空即结束。处方最多 %d 味。\n", MENU_MAX_ITEMS);
    while (item_count < MENU_MAX_ITEMS) {
        char med_id[sizeof(items[0].med_id)];
        char dosage[sizeof(items[0].dosage)];
        Medicine med;
        int qty;
        int med_ret;

        printf("\n--- 第 %d 味药 ---\n", item_count + 1);
        cli_prompt("药品编号（直接回车结束）：", med_id, sizeof(med_id));
        if (med_id[0] == '\0') break;

        memset(&med, 0, sizeof(med));
        med_ret = medicine_query_by_id(db, med_id, &med);
        if (med_ret == -4) {
            printf("没有编号为 %s 的药品，请重新输入。\n", med_id);
            continue;
        } else if (med_ret != 0) {
            printf("查询药品失败：%s\n", err_text(med_ret));
            continue;
        }

        cli_prompt_int("数量：", &qty);
        if (qty <= 0) {
            printf("数量必须大于 0，本味药不加入。\n");
            continue;
        }
        if (qty > med.stock) {
            printf("库存不足：%s 当前库存 %d，本次要 %d。本味药不加入。\n",
                   med.med_name, med.stock, qty);
            continue;
        }

        cli_prompt("用法用量（如 每日3次，每次1粒）：", dosage, sizeof(dosage));

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
        print_presc_items(items, item_count);
        for (i = 0; i < item_count; i++) total += items[i].subtotal;
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

static void prescription_menu(DB_HANDLE* db, const LoginUser* user) {
    int choice = 0;
    int r;

    printf("\n--- 开处方 ---\n");
    printf("  1. 新开处方\n");
    printf("  2. 查看/作废已有处方\n");
    printf("  0. 返回\n");
    printf("请输入选项[0-2]：");
    fflush(stdout);

    r = cli_read_choice(&choice);
    if (r != 0) { printf("输入无效，已返回。\n"); return; }

    switch (choice) {
        case 1: prescription_new(db, user, 0, NULL);  break;
        case 2: prescription_review(db, user, NULL);  break;
        case 0: break;
        default: printf("无效选项\n");                break;
    }
}

static void do_visit(DB_HANDLE* db, const LoginUser* user) {
    Appointment* list = NULL;
    int count = 0;
    int ret;
    int i;
    int found = 0;
    VisitRecord v;
    char complaint_default[sizeof(v.chief_complaint)];

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
        char        slot[sizeof(list[i].time_slot)];
        char        symptom[sizeof(list[i].symptom)];
        char        id_txt[16];
        const int   w[] = {8, 6, 12, 0};
        const char* cells[4];

        snprintf(id_txt, sizeof(id_txt), "[%d]", list[i].appoint_id);
        cells[0] = id_txt;
        cells[1] = one_line_placeholder(list[i].time_slot, slot, sizeof(slot), "未填时段");
        cells[2] = list[i].patient_id;
        cells[3] = one_line_placeholder(list[i].symptom, symptom, sizeof(symptom), "未填症状");
        table_row(cells, w, 4);
    }

    memset(&v, 0, sizeof(v));
    complaint_default[0] = '\0';
    cli_prompt_int("请输入要接诊的预约编号（0 取消）：", &v.appoint_id);
    if (v.appoint_id <= 0) {
        printf("已取消接诊。\n");
        appointment_list_free(list);
        return;
    }

    for (i = 0; i < count; i++) {
        if (list[i].appoint_id == v.appoint_id) {
            found = 1;
            snprintf(v.patient_id, sizeof(v.patient_id), "%s", list[i].patient_id);
            snprintf(v.chief_complaint, sizeof(v.chief_complaint), "%s", list[i].symptom);
            snprintf(complaint_default, sizeof(complaint_default), "%s", list[i].symptom);
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
    cli_prompt("主诉：",            v.chief_complaint, sizeof(v.chief_complaint));
    cli_keep_or(complaint_default, v.chief_complaint, sizeof(v.chief_complaint));
    cli_prompt("现病史：",          v.present_illness, sizeof(v.present_illness));
    cli_prompt_double("体温(℃)：",  &v.temp);
    cli_prompt("血压(如 120/80)：", v.blood_pressure,  sizeof(v.blood_pressure));
    cli_prompt_int("心率(次/分)：", &v.heart_rate);

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

        cli_prompt("\n是否现在为该患者开处方？(y/N)：", ans, sizeof(ans));
        if (ans[0] == 'y' || ans[0] == 'Y') {
            prescription_new(db, user, v.visit_id, v.patient_id);
        }

        cli_prompt("是否现在书写病历？(y/N)：", ans, sizeof(ans));
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

static void do_record(DB_HANDLE* db, const LoginUser* user) {
    int visit_id = 0;
    char patient_id[20] = "";

    cli_prompt_int("请输入就诊ID（0 返回）：", &visit_id);
    if (visit_id <= 0) return;

    if (!lookup_my_visit(db, user->id, visit_id, patient_id, sizeof(patient_id))) {
        printf("没有找到就诊ID %d，或这条就诊记录不是你的，请核对后重试。\n", visit_id);
        return;
    }

    write_record(db, user, visit_id, patient_id);
}



void menu_doctor(DB_HANDLE* db, const LoginUser* user) {
    int running = 1;

    if (!db || !user || user->role != 2) return;

    printf("\n========================================\n");
    printf("  医生工作台  %s(%s)\n", user->name, user->id);
    printf("========================================\n");

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

        r = cli_read_choice(&choice);
        if (r == -2) return;
        if (r != 0) { printf("输入无效，请重新输入\n"); continue; }

        switch (choice) {
            case 1: do_visit(db, user);           break;
            case 2: do_my_visits(db, user);       break;
            case 3: prescription_menu(db, user);  break;
            case 4: do_record(db, user);          break;
            case 5: do_medicine_list(db);         break;
            case 6: menu_change_password(db, user); break;
            case 0: running = 0;                  break;
            default: printf("无效选项\n");        break;
        }
    }

    printf("\n已退出医生工作台。\n");
}
