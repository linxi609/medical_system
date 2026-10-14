/* ==========================================================================
 * prescription.h —— M05 处方管理
 * --------------------------------------------------------------------------
 * 负责组：医生组
 * 对应表：t_prescription（主表）、t_presc_detail（明细）、t_medicine（扣库存）
 * 功能：医生接诊后开处方，一次可开多种药品并记录用法用量；处方可作废；
 *       患者查询本人处方。
 *
 * 【数据结构】一张处方 = 一条主表记录 + 多条明细记录。
 *   主表存诊断结果和状态，明细存每一味药的药名、数量、用法、单价和小计。
 *
 * 【计算规则】明细小计 = 数量 × 单价；处方总金额 = 各明细小计之和。
 *   注意 unit_price 是"开方时的单价快照"，药品之后调价也不影响已有处方。
 *
 * 【事务要求】prescription_add 必须把"写主表""写明细""扣减库存"放在同一
 *   事务里。库存不足时整个处方都不能生效，要 ROLLBACK。
 *
 * 【返回值约定】本模块函数使用以下返回值，各函数只会用到其中一部分：
 *    0   成功
 *   -1   数据库连接失败
 *   -2   SQL 执行失败（已回滚）
 *   -4   就诊记录或处方不存在
 *   -5   库存不足，拒绝开方（已回滚）；开方时药品编号查不到也按 -5 处理
 *   -6   越权操作（不是本人开的处方，或处方已作废）
 * ========================================================================== */

#ifndef PRESCRIPTION_H
#define PRESCRIPTION_H

#include "db_mysql.h"

/* 处方主表，对应 t_prescription 的一行 */
typedef struct {
    int  presc_id;          /* 处方 ID，自增主键 */
    int  visit_id;          /* 对应的就诊 ID */
    char patient_id[20];    /* 患者编号 */
    char doctor_id[10];     /* 开方医生编号 */
    char diagnosis[200];    /* 诊断结果 */
    int  status;            /* 状态：1 有效，0 已作废 */
    char create_time[20];   /* 开方时间 */
} Prescription;

/* 处方明细，对应 t_presc_detail 的一行 */
typedef struct {
    int    detail_id;       /* 明细 ID，自增主键 */
    int    presc_id;        /* 所属处方 ID */
    char   med_id[20];      /* 药品编号 */
    int    quantity;        /* 数量 */
    char   dosage[100];     /* 用法用量，如 "每日 3 次，每次 1 粒" */
    double unit_price;      /* 开方时的单价快照 */
    double subtotal;        /* 小计 = quantity × unit_price，由函数计算填入 */
} PrescDetail;

/* --------------------------------------------------------------------------
 * 开处方
 * 参数：db          已连接的数据库句柄
 *       p           [入参+出参] 处方主表信息。除 presc_id 外各字段由调用者填好，
 *                   status 填 1；成功后 presc_id 回写到 p->presc_id
 *       items       [入参+出参] 明细数组，长度 item_count。
 *                   调用者只需填 med_id、quantity、dosage；
 *                   unit_price 和 subtotal 由函数查药品表后填入，
 *                   同时从 t_medicine.stock 扣减对应数量
 *       item_count  明细条数，至少为 1
 * 说明：任一味药库存不足，整张处方都不生效（事务回滚）
 * 返回：0 成功；-5 库存不足（已回滚）；-4 就诊记录不存在；
 *       -2 SQL 执行失败（已回滚）
 * -------------------------------------------------------------------------- */
int prescription_add(DB_HANDLE* db, Prescription* p,
                     PrescDetail* items, int item_count);

/* --------------------------------------------------------------------------
 * 作废处方
 * 参数：db        已连接的数据库句柄
 *       presc_id  处方 ID
 *       doctor_id 发起作废的医生编号，用于校验是不是本人开的
 * 说明：把 status 置 0。注意作废不回滚库存——药已经发给患者了
 * 返回：0 成功；-4 处方不存在；-6 不是本人开的或是已作废；-2 SQL 执行失败
 * -------------------------------------------------------------------------- */
int prescription_void(DB_HANDLE* db, int presc_id, const char* doctor_id);

/* --------------------------------------------------------------------------
 * 查询某患者的全部处方（不含明细，列表展示用）
 * 参数：db         已连接的数据库句柄
 *       patient_id 患者编号
 *       out_list   [出参] 函数内部 malloc 出数组，用完必须调 prescription_list_free
 *       out_count  [出参] 结果条数
 * 返回：0 成功（可能 out_count 为 0）；-2 SQL 执行失败
 * -------------------------------------------------------------------------- */
int prescription_query_by_patient(DB_HANDLE* db, const char* patient_id,
                                  Prescription** out_list, int* out_count);

/* --------------------------------------------------------------------------
 * 按 ID 查询一张处方及其全部明细
 * 参数：db              已连接的数据库句柄
 *       presc_id        处方 ID
 *       out             [出参] 处方主表信息写入此处
 *       out_items       [出参] 函数内部 malloc 出明细数组，
 *                       用完必须调 prescription_detail_free
 *       out_item_count  [出参] 明细条数
 * 返回：0 成功；-4 处方不存在；-2 SQL 执行失败
 * -------------------------------------------------------------------------- */
int prescription_query_by_id(DB_HANDLE* db, int presc_id,
                             Prescription* out,
                             PrescDetail** out_items, int* out_item_count);

/* --------------------------------------------------------------------------
 * 查询全部处方（管理员用，对应管理员主菜单"处方查询"）
 * 参数：db         已连接的数据库句柄
 *       out_list   [出参] 函数内部 malloc 出数组，
 *                  用完必须调 prescription_list_free
 *       out_count  [出参] 结果条数
 * 返回：0 成功（可能 out_count 为 0）；-2 SQL 执行失败
 * -------------------------------------------------------------------------- */
int prescription_query_all(DB_HANDLE* db, Prescription** out_list, int* out_count);

/* --------------------------------------------------------------------------
 * 释放 prescription_query_by_patient 返回的数组
 * 参数：list  prescription_query_by_patient 得到的数组，传 NULL 也安全
 * -------------------------------------------------------------------------- */
void prescription_list_free(Prescription* list);

/* --------------------------------------------------------------------------
 * 释放 prescription_query_by_id 返回的明细数组
 * 参数：items  prescription_query_by_id 得到的明细数组，传 NULL 也安全
 * -------------------------------------------------------------------------- */
void prescription_detail_free(PrescDetail* items);

#endif /* PRESCRIPTION_H */
