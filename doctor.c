#include "doctor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 把字符串转义后拼成 SQL 字面量（带两边的单引号），写进 dst。
 * 直接拼字符串的话，用户输入里带个单引号就会把 SQL 拼坏，所以统一走一遍
 * mysql_real_escape_string。和 medical_system 各模块里的同名辅助函数一致。
 * src 传 NULL 时按空字符串处理，dst 装不下时返回 NULL。 */
static char* escape_str(DB_HANDLE* db, const char* src, char* dst, size_t dstsz)
{
    size_t n;
    if (!dst || dstsz < 3) return NULL;
    if (!src) src = "";
    n = mysql_real_escape_string(db->mysql, dst + 1, src, (unsigned long)strlen(src));
    if (n + 3 > dstsz) return NULL;
    dst[0] = '\'';
    dst[n + 1] = '\'';
    dst[n + 2] = '\0';
    return dst;
}

int doctor_add(DB_HANDLE* db, Doctor* d)
{
    if (!db || !d)
        return -1;

    // 查询最大医生编号
    DB_RES* res = db_query(db, "SELECT doctor_id FROM t_doctor ORDER BY doctor_id DESC LIMIT 1");
    int max_num = 0;
    if (res != NULL)
    {
        DB_ROW* row = db_fetch_row(res);
        if (row != NULL && row->fields[0] != NULL)
        {
            const char* id_str = row->fields[0] + 1;
            max_num = atoi(id_str);
        }
        db_res_free(res);
    }
    int new_num = max_num + 1;
    snprintf(d->doctor_id, sizeof(d->doctor_id), "D%03d", new_num);

    // 各字符串字段转义并加引号（doctor_id 是上面自己生成的 "Dxxx"，直接拼）
    char esc_name[sizeof(d->name) * 2 + 3];
    char esc_gender[sizeof(d->gender) * 2 + 3];
    char esc_title[sizeof(d->title) * 2 + 3];
    char esc_dept[sizeof(d->dept) * 2 + 3];
    char esc_phone[sizeof(d->phone) * 2 + 3];
    char esc_username[sizeof(d->username) * 2 + 3];
    char esc_password[sizeof(d->password) * 2 + 3];

    if (!escape_str(db, d->name,     esc_name,     sizeof(esc_name))     ||
        !escape_str(db, d->gender,   esc_gender,   sizeof(esc_gender))   ||
        !escape_str(db, d->title,    esc_title,    sizeof(esc_title))    ||
        !escape_str(db, d->dept,     esc_dept,     sizeof(esc_dept))     ||
        !escape_str(db, d->phone,    esc_phone,    sizeof(esc_phone))    ||
        !escape_str(db, d->username, esc_username, sizeof(esc_username)) ||
        !escape_str(db, d->password, esc_password, sizeof(esc_password)))
    {
        return -2;
    }

    // 插入数据库
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_doctor(doctor_id,name,gender,title,dept,phone,username,password,status) "
        "VALUES('%s',%s,%s,%s,%s,%s,%s,%s,1)",
        d->doctor_id,
        esc_name,
        esc_gender,
        esc_title,
        esc_dept,
        esc_phone,
        esc_username,
        esc_password
    );

    int ret = db_execute(db, sql);
    if (ret < 0)
    {
        return -3;
    }
    return 0;

}

int doctor_delete(DB_HANDLE* db, const char* doctor_id)
{
	if (!db || !doctor_id)
        {return -1;}
	int num=atoi(doctor_id+1);

	char esc_id[64];
	if (!escape_str(db, doctor_id, esc_id, sizeof(esc_id)))
		return -2;

	char sql[256];
	snprintf(sql, sizeof(sql), "DELETE FROM t_doctor WHERE doctor_id=%s", esc_id);
	int ret = db_execute(db, sql);
	if(ret<0)
	{
		printf("删除失败");
	}

	// 后续编号整体前移一位：每行各自减 1。
	// 注意不能写成固定的 LPAD(num-1,...)，那样所有行会被改成同一个编号，
	// 撞上已有的编号导致整条 UPDATE 失败（主键冲突），编号就前移不了。
	snprintf(sql, sizeof(sql),
	    "UPDATE t_doctor SET doctor_id=CONCAT('D',LPAD(CAST(SUBSTRING(doctor_id,2) AS UNSIGNED)-1,3,'0')) "
	    "WHERE CAST(SUBSTRING(doctor_id,2) AS UNSIGNED) > %d",
	    num);
	db_execute(db, sql);
	return 0;
}

int doctor_update(DB_HANDLE* db, const Doctor* d)
{
    if (!db || !d)
        return -1;

    char esc_id[64];
    if (!escape_str(db, d->doctor_id, esc_id, sizeof(esc_id)))
        return -2;

    // 先确认这个医生存在，不存在返回 -4
    char sql[1024];
    snprintf(sql, sizeof(sql), "SELECT doctor_id FROM t_doctor WHERE doctor_id=%s", esc_id);
    DB_RES* res = db_query(db, sql);
    if (res == NULL)
        return -2;
    DB_ROW* row = db_fetch_row(res);
    db_res_free(res);
    if (row == NULL)
        return -4;

    // 改姓名/性别/职称/科室/电话；密码传空串表示不修改，保持原值
    // status 由 doctor_add / doctor_delete 管，这里不动
    char esc_name[sizeof(d->name) * 2 + 3];
    char esc_gender[sizeof(d->gender) * 2 + 3];
    char esc_title[sizeof(d->title) * 2 + 3];
    char esc_dept[sizeof(d->dept) * 2 + 3];
    char esc_phone[sizeof(d->phone) * 2 + 3];
    char esc_password[sizeof(d->password) * 2 + 3];

    if (!escape_str(db, d->name,     esc_name,     sizeof(esc_name))     ||
        !escape_str(db, d->gender,   esc_gender,   sizeof(esc_gender))   ||
        !escape_str(db, d->title,    esc_title,    sizeof(esc_title))    ||
        !escape_str(db, d->dept,     esc_dept,     sizeof(esc_dept))     ||
        !escape_str(db, d->phone,    esc_phone,    sizeof(esc_phone))    ||
        !escape_str(db, d->password, esc_password, sizeof(esc_password)))
    {
        return -2;
    }

    snprintf(sql, sizeof(sql),
        "UPDATE t_doctor SET name=%s,gender=%s,title=%s,dept=%s,phone=%s,"
        "password=COALESCE(NULLIF(%s,''),password) WHERE doctor_id=%s",
        esc_name,
        esc_gender,
        esc_title,
        esc_dept,
        esc_phone,
        esc_password,
        esc_id
    );
    int ret = db_execute(db, sql);
    if (ret < 0)
        return -2;
    return 0;
}

/* 把查询结果的一行按列顺序填进 Doctor 结构体（SQL NULL 按空串处理） */
static void doctor_fill_row(DB_ROW* row, Doctor* d)
{
    memset(d, 0, sizeof(Doctor));
    snprintf(d->doctor_id, sizeof(d->doctor_id), "%s", row->fields[0] ? row->fields[0] : "");
    snprintf(d->name,      sizeof(d->name),      "%s", row->fields[1] ? row->fields[1] : "");
    snprintf(d->gender,    sizeof(d->gender),    "%s", row->fields[2] ? row->fields[2] : "");
    snprintf(d->title,     sizeof(d->title),     "%s", row->fields[3] ? row->fields[3] : "");
    snprintf(d->dept,      sizeof(d->dept),      "%s", row->fields[4] ? row->fields[4] : "");
    snprintf(d->phone,     sizeof(d->phone),     "%s", row->fields[5] ? row->fields[5] : "");
    snprintf(d->username,  sizeof(d->username),  "%s", row->fields[6] ? row->fields[6] : "");
    snprintf(d->password,  sizeof(d->password),  "%s", row->fields[7] ? row->fields[7] : "");
    d->status = row->fields[8] ? atoi(row->fields[8]) : 0;
}

int doctor_query_by_id(DB_HANDLE* db, const char* doctor_id, Doctor* out)
{
    if (!db || !doctor_id || !out)
        return -1;

    char esc_id[64];
    if (!escape_str(db, doctor_id, esc_id, sizeof(esc_id)))
        return -2;

    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT doctor_id,name,gender,title,dept,phone,username,password,status "
        "FROM t_doctor WHERE doctor_id=%s", esc_id);
    DB_RES* res = db_query(db, sql);
    if (res == NULL)
        return -2;

    DB_ROW* row = db_fetch_row(res);
    if (row == NULL)
    {
        db_res_free(res);
        return -4;
    }
    doctor_fill_row(row, out);
    db_res_free(res);
    return 0;
}

int doctor_query_all(DB_HANDLE* db, Doctor** out_list, int* out_count)
{
    if (out_list)  *out_list  = NULL;
    if (out_count) *out_count = 0;
    if (!db || !out_list || !out_count)
        return -1;

    // 挂号时用，只列在职医生（status=1）
    DB_RES* res = db_query(db,
        "SELECT doctor_id,name,gender,title,dept,phone,username,password,status "
        "FROM t_doctor WHERE status=1 ORDER BY doctor_id");
    if (res == NULL)
        return -2;

    int count = res->row_cnt;
    if (count > 0)
    {
        Doctor* list = (Doctor*)malloc(count * sizeof(Doctor));
        if (list == NULL)
        {
            db_res_free(res);
            return -2;
        }
        DB_ROW* row;
        int i = 0;
        while (i < count && (row = db_fetch_row(res)) != NULL)
        {
            doctor_fill_row(row, &list[i]);
            i++;
        }
        *out_list  = list;
        *out_count = i;
    }
    db_res_free(res);
    return 0;
}

void doctor_list_free(Doctor* list)
{
    free(list);
}
