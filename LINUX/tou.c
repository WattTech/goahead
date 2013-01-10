/**
 * @file tou.c 电量历史数据文件
 * @param mtr_no
 * @param range
 * @param tou
 * @return
 */
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "../uemf.h"
#include "../wsIntrn.h"
#include "tou.h"
#include "web_err.h"
#include "type.h"
/**
 * 读取一个电表的一段时间段的电量数据.
 * @param start
 * @param end
 * @param mtr_no
 * @return
 */
int load_tou_dat(u32 mtr_no, TimeRange const range, stTou* ptou, webs_t wp)
{
	stTouFilehead filehead;
	if (range.e<range.s||range.e==0||range.s==0) {
		web_errno = tou_timerange_err;
		return ERR;
	}
	char file[256] = { 0 };
	struct tm t;
	struct tm st_today_0;
	time_t t_today_0;
	u32 stime = range.s;     //开始时刻
	u32 etime = range.e;     //结束时刻
	time_t t2;     //时刻
	u32 mincycle = 0;
	stTou tou;
	//time_t t_cur = range.s;
	FILE*fp;
	int flen;
	int i = 0;
	//从开始时刻到结束时刻,按分钟遍历,步距为周期,可变.[start,end]两边闭区间
	for (t2 = stime; t2<=etime; /*t2 += (mincycle * 60)*/) {
		//int count;
		//for(count=0;count<2;count++){
		Start:
		#if __arm__ ==2
		gmtime_r(&t2,&t);
		gmtime_r(&t2,&st_today_0);
		printf("gmtime_r %02d-%02d %02d:%02d %s t.tm_gmtoff=%d \n",
				t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,
				t.tm_zone,t.tm_gmtoff);
#else
		localtime_r(&t2, &t);
		localtime_r(&t2, &st_today_0);
		printf("localtime_r %02d-%02d %02d:%02d %s t.tm_gmtoff=%ld \n",
		                t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min,
		                t.tm_zone, t.tm_gmtoff);
#endif
		sprintf(file, "%s/mtr%03d%02d%02d.%s", TOU_DAT_DIR, mtr_no, 0,
		                t.tm_mday, TOU_DAT_SUFFIX);
		fp = fopen(file, "r");
		if (fp==NULL) {		//这一天没有数据,直接跳到次日零点
			printf("%d:%04d-%02d-%02d没有数据文件\n",
			                mtr_no, t.tm_year+1900, t.tm_mon+1
			                                , t.tm_mday);
			web_errno = open_tou_file;
			t.tm_hour = 0;
			t.tm_min = 0;
			t.tm_sec = 0;
			t2 = mktime(&t);
			t2 += (60*60*24);
			continue;
			return ERR;
		}
		fseek(fp, 0, SEEK_END);
		flen = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		int n = fread(&filehead, sizeof(stTouFilehead), 1, fp);
		if (n!=1) {
			web_errno = read_tou_file_filehead;
			//continue;
			return ERR;
		}
		///@todo 检查文件头中是否和请求的日期相一致.
		int cycle = (filehead.save_cycle_hi*256)
		                +filehead.save_cycle_lo;
		mincycle = cycle;
		//stTou at[24 * 60 / cycle];

		int t_mod = t2%(mincycle*60);     //向上园整至采样周期.
		if (t_mod!=0) {     //需要园整
			t2 += (mincycle*60-t_mod);
		}
		//}
		/**@todo 判断开始时间+周期是否跨度到了第二天,如果跨度到第二天则需要
		 打开另一个数据文件.
		 */
//		t_cur=t_cur/60*60;
//		gmtime_r(&t_cur,&t);//本日凌晨
//		t_cur+=(t_cur%(cycle*60));
//		gmtime_r(&t_cur,&t);//向上元整后的开始时刻.
		st_today_0.tm_hour = 0;
		st_today_0.tm_min = 0;
		st_today_0.tm_sec = 0;
		t_today_0 = mktime(&st_today_0);
		if (t2-t_today_0>=(60*60*24)) {     //t2已经时间跨过本日了.次日则文件等等需要重新打开.
			goto Start;
		}
		///移动文件指针,指向开始的数据结构.
		int DeltaSec = t2-t_today_0;     //本采样时刻举今日凌晨几秒
		int NumCycle = DeltaSec/(mincycle*60);     //从凌晨开始向后偏移几个采样周期
		int offset = sizeof(stTou)*NumCycle;     //每个样本长度*采样个数
		fseek(fp, offset, SEEK_CUR);     ///当前位置为除去文件头的第一个数据体.

		if (ftell(fp)>=flen) {
			printf("本日的数据不够.filesize=%d,fseek=%ld:%s\n", flen,
			                ftell(fp), file);
			t2 += (mincycle*60);
			continue;
		}
		while (ftell(fp)<flen&&t2<=etime) {
			//t_cur += cycle * 60;
			int n = fread(&tou, sizeof(stTou), 1, fp);
			if (n!=1) {
				web_errno = read_tou_file_dat;
				return ERR;
			}
			//成功
			write2web( t2,  wp,   tou, i, mtr_no);
			i++;
			t2 += (mincycle*60);
		}
		//t2 += ((i-1)*(mincycle * 60));
	}
	return 0;
}
/**
 * 像web页面写东西
 * @param t2
 * @param wp
 * @param tou
 * @param i
 * @param mtr_no
 * @return
 */
int write2web(time_t t2, webs_t wp, const stTou tou,int i,int mtr_no)
{
	struct tm t;
#if __arm__ ==2
	gmtime_r(&t2,&t);
#else
	localtime_r(&t2, &t);
#endif
	websWrite(wp, T("<tr>"));
	websWrite(wp, T("<td%s>%d</td>"), TD_CLASS, mtr_no);
	websWrite(wp, T("<td%s>%d</td>"), TD_CLASS, i);
	websWrite(wp,
	                T("<td%s>%04d-%02d-%02d %02d:%02d:%02d %s</td>"),
	                TD_CLASS, t.tm_year+1900,
	                t.tm_mon+1, t.tm_mday, t.tm_hour,
	                t.tm_min, t.tm_sec, t.tm_zone);
	webWrite1Tou(wp, tou);
	websWrite(wp, T("</tr>\n"));
	return 0;
}
//写一条电量Tou数据
int webWrite1Tou(webs_t wp, const stTou tou)
{
	///用于显示无效的样式,有效的使用默认的
	const char *iv = " style=\"text-decoration:line-through;\" ";
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.FA.total.iv ? "" : iv, tou.FA.total.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.FA.tip.iv ? "" : iv, tou.FA.tip.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.FA.peak.iv ? "" : iv, tou.FA.peak.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.FA.flat.iv ? "" : iv, tou.FA.flat.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.FA.valley.iv ? iv : "", tou.FA.valley.val);

	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.RA.total.iv ? "" : iv, tou.RA.total.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.RA.tip.iv ? "" : iv, tou.RA.tip.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.RA.peak.iv ? "" : iv, tou.RA.peak.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.RA.flat.iv ? "" : iv, tou.RA.flat.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.RA.valley.iv ? iv : "", tou.RA.valley.val);

	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.FR.total.iv ? "" : iv, tou.FR.total.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.FR.tip.iv ? "" : iv, tou.FR.tip.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.FR.peak.iv ? "" : iv, tou.FR.peak.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.FR.flat.iv ? "" : iv, tou.FR.flat.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.FR.valley.iv ? iv : "", tou.FR.valley.val);

	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.RR.total.iv ? "" : iv, tou.RR.total.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.RR.tip.iv ? "" : iv, tou.RR.tip.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.RR.peak.iv ? "" : iv, tou.RR.peak.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.RR.flat.iv ? "" : iv, tou.RR.flat.val);
	websWrite(wp, T("<td%s %s>%d</td>"), TD_CLASS,
	                tou.RR.valley.iv ? iv : "", tou.RR.valley.val);

	return 0;
}
