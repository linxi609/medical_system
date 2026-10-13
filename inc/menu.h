/* ==========================================================================
 * menu.h —— 三个角色的主菜单
 * --------------------------------------------------------------------------
 * 负责组：各自角色的菜单由对应组负责
 *     menu_admin    → 管理员组
 *     menu_doctor   → 医生组
 *     menu_patient  → 患者组
 *
 * 说明：main.c 在登录成功后按 user->role 调用对应的菜单函数，登录失败则
 *       回到主菜单。三个菜单函数之间互不调用，各自独立，不会互相冲突。
 *
 * 每个菜单函数内部都是一个循环，结构大致是：
 *     循环 {
 *         显示菜单
 *         读用户选择（输入非法要能兜住，别让程序卡死）
 *         调用对应模块的函数
 *         根据返回值给出提示（成功/失败原因）
 *      } 直到用户选择"返回登录"或"退出系统"
 *
 * 菜单项里用到的函数都已在 inc/ 下声明好，照着调即可。
 * ========================================================================== */

#ifndef MENU_H
#define MENU_H

#include "db_mysql.h"
#include "auth.h"

/* --------------------------------------------------------------------------
 * 管理员主菜单
 * 参数：db    已连接的数据库句柄
 *       user  当前登录的管理员（用于显示欢迎语、修改密码时定位本人）
 * 菜单项（见文档 1.7 界面原型）：
 *       1. 患者档案管理（查/改/注销/导出）
 *       2. 医生管理
 *       3. 预约查询
 *       4. 就诊记录查询
 *       5. 处方查询
 *       6. 药品管理（盘点/查询/新增/入库/出库/调价/删除）
 *       7. 病历查询
 *       8. 修改密码
 *       0. 退出系统
 * 对应函数：patient_query_all / patient_update_self / patient_delete /
 *           patient_export / doctor_* / appointment_query_all /
 *           visit_query_all / prescription_query_all / medicine_* /
 *           medical_record_query_all / user_change_password
 * -------------------------------------------------------------------------- */
void menu_admin(DB_HANDLE* db, const LoginUser* user);

/* --------------------------------------------------------------------------
 * 医生工作台
 * 参数：db    已连接的数据库句柄
 *       user  当前登录的医生（医生编号取自 user->id）
 * 菜单项（见文档 1.7 界面原型）：
 *       进入时先列出当日待就诊预约（appointment_query_by_doctor_today）
 *       1. 接诊（输入预约编号）→ visit_add
 *       2. 我的就诊记录        → visit_query_by_doctor
 *       3. 开处方              → prescription_add，药品和库存用 medicine_query_*
 *       4. 书写病历            → medical_record_add / medical_record_update
 *       5. 查询药品库存        → medicine_query_all
 *       6. 修改密码            → user_change_password
 *       0. 返回登录
 * -------------------------------------------------------------------------- */
void menu_doctor(DB_HANDLE* db, const LoginUser* user);

/* --------------------------------------------------------------------------
 * 患者工作台
 * 参数：db    已连接的数据库句柄
 *       user  当前登录的患者（患者编号取自 user->id）
 * 菜单项（见文档 1.7 界面原型）：
 *       1. 挂号（选医生/日期/时段）→ doctor_query_all 选医生，appointment_add 挂号
 *       2. 我的预约（可取消未就诊预约）→ appointment_query_by_patient /
 *          appointment_cancel
 *       3. 我的就诊记录 → visit_query_by_patient
 *       4. 我的处方     → prescription_query_by_patient，看明细用
 *          prescription_query_by_id
 *       5. 我的病历     → medical_record_query_by_patient
 *       6. 修改个人资料/密码 → patient_update_self / user_change_password
 *       0. 返回登录
 * -------------------------------------------------------------------------- */
void menu_patient(DB_HANDLE* db, const LoginUser* user);

#endif /* MENU_H */
