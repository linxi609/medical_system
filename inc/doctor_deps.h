/* ==========================================================================
 * doctor_deps.h —— 医生模块依赖的跨组函数（仅本次新增）
 * --------------------------------------------------------------------------
 * 负责组：医生组
 * 作者：陈翔宇
 * 创建日期：2026-10-13
 *
 * 【为什么要单独有这个文件】
 *   医生工作台要用到下面几个函数，但它们按 README 的分工不属于医生组：
 *     appointment_query_by_doctor_today  -> M03，患者组的 appointment.c
 *     medicine_query_all / _by_id        -> M06，管理员组的 medicine.c
 *   那两个文件里目前还是空壳（只 return 0），工作台直接调用会拿到空列表。
 *
 *   为了"不改别人的文件"，这里把医生组用到的这几个函数放在独立文件
 *   src/doctor_deps.c 里实现，不改 appointment.c / medicine.c 一行。
 *
 * 【函数声明在哪】
 *   被顶替的那几个函数（appointment_query_by_doctor_today、
 *   appointment_list_free、medicine_query_all、medicine_query_by_id、
 *   medicine_list_free）的声明仍然只在 inc/appointment.h 和 inc/medicine.h
 *   里，本文件不重复声明——重复声明除了触发 -Wredundant-decls，
 *   还容易和原文件产生"两处签名各改一半"的隐患。
 *   签名一个字没变，所以以后把实现搬回原文件，调用方完全不用改。
 *
 *   本文件只需要声明医生组自己新加的两条查询（它们不属于任何已有模块）。
 *
 * 【以后患者组 / 管理员组写完了怎么办】
 *   把他们写的实现填回 appointment.c / medicine.c 之后，
 *   删掉本头文件和 src/doctor_deps.c 即可，调用方不用改；
 *   同时记得把 Makefile 里 SRCS 那段的过滤去掉（那里有说明）。
 * ========================================================================== */

#ifndef DOCTOR_DEPS_H
#define DOCTOR_DEPS_H

#include "db_mysql.h"
#include "appointment.h"      /* appointment_query_by_doctor_today / _list_free 的声明 */
#include "medicine.h"         /* medicine_query_all / _by_id / _list_free 的声明 */
#include "medical_record.h"

/* --------------------------------------------------------------------------
 * 按就诊 ID 查病历（医生工作台内部用，M07 的补充查询）
 * 说明：medical_record_update 只按 record_id 定位，医生在"书写病历"里
 *       需要按就诊 ID 找回病历才能改，所以补这一条查询。
 *       out 由调用方提供；找到填 1 并回填内容，没找到填 0，SQL 失败返回 -2
 * 返回：1 找到；0 没找到；-1 未连接；-2 SQL 失败
 * -------------------------------------------------------------------------- */
int medical_record_find_by_visit(DB_HANDLE* db, int visit_id, MedicalRecord* out);

/* --------------------------------------------------------------------------
 * 按病历 ID 查单条病历（医生工作台内部用）
 * 返回：1 找到；0 没找到；-1 未连接；-2 SQL 失败
 * -------------------------------------------------------------------------- */
int medical_record_find_by_id(DB_HANDLE* db, int record_id, MedicalRecord* out);

#endif /* DOCTOR_DEPS_H */
