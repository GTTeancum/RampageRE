#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ledger.h"
#include CITY_SOURCE
#include "fixture.h"
static int owned_books(const City*c){int n=0;for(int i=0;i<c->nbooks;i++)n+=c->books[i].ok==1;return n;}
static void sweep(const Vol*v,const char*key,const char*label){
 size_t baseline=ledger_bytes,live=ledger_live;City*c=fresh(v);size_t b=ledger_calls;LoadedBook*lb=load_book(c,key);assert(lb&&lb->ok&&lb->book.b==&lb->b);
 size_t count=ledger_calls-b;uint64_t h=hash(lb->b.file,lb->b.file_size);size_t calls=ledger_calls,reads=ledger_read_calls,bytes=ledger_bytes;
 for(int i=0;i<100;i++)assert(load_book(c,key)==lb);
 assert(ledger_calls==calls&&ledger_read_calls==reads&&ledger_bytes==bytes&&c->nbooks==1);city_close(c);assert(ledger_bytes==baseline&&ledger_live==live);
 for(size_t f=1;f<=count;f++){
  c=fresh(v);size_t before=ledger_bytes,hits=ledger_fail_hits;ledger_fail_at=ledger_calls+f;assert(!load_book(c,key));ledger_reset_fault();assert(ledger_fail_hits==hits+1);assert(c->nbooks==0&&city_load_failed(c)&&!c->books[0].b.file&&ledger_bytes==before);
  calls=ledger_calls;reads=ledger_read_calls;for(int j=0;j<100;j++)assert(!load_book(c,key));assert(calls==ledger_calls&&reads==ledger_read_calls);
  city_activate(c);city_new_world(c);assert(city_load_failed(c));city_reset_load_failure(c);assert(!city_load_failed(c));lb=load_book(c,key);assert(lb&&lb->book.b==&lb->b&&hash(lb->b.file,lb->b.file_size)==h);city_close(c);assert(ledger_bytes==baseline&&ledger_live==live);
 }
 printf("sweep label=%s allocations=%zu faults=%zu sticky_no_retry=1 same_city_explicit_recovery=1 stable_in_place=1 zero_delta=1\n",label,count,count);
}
static void synthetic(void){
 uint8_t raw[1024];VolFile vf={0};Vol v={0};vf.name="Present.bdp";vf.size=1024;v.files=&vf;v.count=1;
 for(int w=0;w<2;w++){fixture(raw,w,0,0);FILE*f=tmpfile();assert(f&&fwrite(raw,1,1024,f)==1024);v.f=f;vf.name=w?"Present.bdg":"Present.bdp";sweep(&v,w?"Present.bdg":"Present",w?"synthetic-wii":"synthetic-ps2");fclose(f);}
 fixture(raw,0,0,0);FILE*f=tmpfile();assert(f&&fwrite(raw,1,1024,f)==1024);v.f=f;vf.name="Present.bdp";City*c=fresh(&v);size_t calls=ledger_calls,reads=ledger_read_calls;
 for(int i=0;i<100;i++){char key[64];snprintf(key,64,"Missing%d",i);assert(!load_book(c,key)&&!city_load_failed(c));}assert(c->nbooks==MAX_BOOKS&&!owned_books(c)&&calls==ledger_calls&&reads==ledger_read_calls);size_t finds=ledger_find_calls;for(int i=0;i<100;i++)assert(!load_book(c,"Missing99"));assert(ledger_find_calls==finds);LoadedBook*kept=load_book(c,"Present");assert(kept&&owned_books(c)==1&&c->nbooks==MAX_BOOKS);vf.name="Added.bdp";assert(!load_book(c,"NewFile"));vf.name="NewFile.bdp";assert(!load_book(c,"NewFile"));for(int repeat=0;repeat<3;repeat++)city_reset_load_failure(c);assert(load_book(c,"NewFile")&&kept->book.b==&kept->b&&group_num(book_group(&kept->book,"Root"),"Answer",0)==42);city_close(c);fclose(f);vf.name="Present.bdp";
 fixture(raw,0,1,0);f=tmpfile();assert(f&&fwrite(raw,1,1024,f)==1024);v.f=f;c=fresh(&v);size_t bytes=ledger_bytes;assert(!load_book(c,"Present")&&city_load_failed(c)&&!c->nbooks&&bytes==ledger_bytes);city_close(c);fclose(f);
 fixture(raw,0,0,0);f=tmpfile();assert(f);v.f=f;c=fresh(&v);assert(!load_book(c,"Present")&&city_load_failed(c));assert(fwrite(raw,1,1024,f)==1024);fflush(f);assert(!load_book(c,"Present"));city_reset_load_failure(c);assert(load_book(c,"Present"));city_close(c);fclose(f);
 raw[0]='X';f=tmpfile();assert(f&&fwrite(raw,1,1024,f)==1024);v.f=f;c=fresh(&v);bytes=ledger_bytes;assert(!load_book(c,"Present")&&city_load_failed(c)&&bytes==ledger_bytes);city_close(c);fclose(f);
 /* Full success table fails closed, but misses never fill it. All 32 borrowed Books stay valid. */
 fixture(raw,0,0,0);f=tmpfile();assert(f&&fwrite(raw,1,1024,f)==1024);v.f=f;VolFile rows[33]={{0}};char names[33][32];v.files=rows;v.count=33;c=fresh(&v);LoadedBook*first=NULL;Group borrowed[32];
 for(int i=0;i<33;i++){snprintf(names[i],32,"Book%d.bdp",i);rows[i].name=names[i];rows[i].size=1024;char key[32];snprintf(key,32,"Book%d",i);if(i==32)assert(!load_book(c,"GenuinelyAbsentFull")&&!city_load_failed(c));LoadedBook*b=load_book(c,key);if(i<32){assert(b);borrowed[i]=book_group(&b->book,"Root");if(!i)first=b;assert(first->book.b==&first->b&&group_num(book_group(&first->book,"Root"),"Answer",0)==42);}else assert(!b&&city_load_failed(c)&&c->nbooks==32);}
 city_reset_load_failure(c);for(int i=0;i<32;i++){assert(borrowed[i].book==&c->books[i].book&&borrowed[i].book->b==&c->books[i].b&&group_num(borrowed[i],"Answer",0)==42);}assert(!load_book(c,"AbsentAfterReset")&&!city_load_failed(c));
 city_close(c);fclose(f);assert(!ledger_live&&!ledger_bytes);
 puts("absence_capacity=PASS rejected_book_zero_retention=PASS short_read_reset=PASS corrupt_header=PASS success_capacity=PASS");
}
static void mixed_slots(void){
 uint8_t raw[1024];fixture(raw,0,0,0);FILE*f=tmpfile();assert(f&&fwrite(raw,1,1024,f)==1024);VolFile row={0};row.name="Present.bdp";row.size=1024;Vol v={0};v.f=f;v.files=&row;v.count=1;City*c=fresh(&v);
 /* A success behind a negative hole must never move when reset clears that hole. */
 assert(!load_book(c,"Hole"));LoadedBook*stable=load_book(c,"Present");assert(stable==&c->books[1]);Group g=book_group(&stable->book,"Root");city_reset_load_failure(c);assert(c->nbooks==2&&!c->books[0].name[0]&&stable->book.b==&stable->b);row.name="Second.bdp";LoadedBook*second=load_book(c,"Second");assert(second==&c->books[0]&&g.book==&c->books[1].book&&group_num(g,"Answer",0)==42);city_close(c);
 /* The historical 17-negative working set fits beside one stable success. */
 row.name="Present.bdp";c=fresh(&v);stable=load_book(c,"Present");size_t finds=ledger_find_calls;for(int round=0;round<10;round++)for(int k=0;k<17;k++){char name[32];snprintf(name,32,"Missing%d",k);assert(!load_book(c,name)&&!city_load_failed(c));}assert(ledger_find_calls-finds==17&&owned_books(c)==1&&load_book(c,"Present")==stable);city_close(c);
 /* All direct loader allocation points also fail cleanly when reusing an occupied
  * negative slot. A later explicit attempt reuses the same hole without moving its success. */
 size_t total_faults=0;
 for(int wii=0;wii<2;wii++){
 fixture(raw,wii,0,0);assert(!fseek(f,0,SEEK_SET)&&fwrite(raw,1,1024,f)==1024);fflush(f);
 c=fresh(&v);size_t begin=ledger_calls;assert(load_book(c,"Present"));size_t allocations=ledger_calls-begin;city_close(c);total_faults+=allocations;
 for(size_t fault=1;fault<=allocations;fault++){
  row.name="Keep.bdp";c=fresh(&v);stable=load_book(c,"Keep");g=book_group(&stable->book,"Root");for(int k=0;k<31;k++){char name[32];snprintf(name,32,"Absent%d",k);assert(!load_book(c,name));}assert(c->nbooks==32&&owned_books(c)==1);row.name="Present.bdp";size_t bytes=ledger_bytes,hits=ledger_fail_hits;ledger_fail_at=ledger_calls+fault;assert(!load_book(c,"Present"));ledger_reset_fault();assert(city_load_failed(c)&&ledger_fail_hits==hits+1&&ledger_bytes==bytes&&owned_books(c)==1&&c->nbooks==32&&!c->books[1].name[0]);assert(g.book==&c->books[0].book&&group_num(g,"Answer",0)==42);size_t reads=ledger_read_calls;assert(!load_book(c,"Present")&&reads==ledger_read_calls);city_reset_load_failure(c);LoadedBook*recovered=load_book(c,"Present");assert(recovered==&c->books[1]&&g.book==&c->books[0].book&&g.book->b==&stable->b&&owned_books(c)==2);city_close(c);
 }
 }
 fclose(f);assert(!ledger_live&&!ledger_bytes);assert(total_faults==15);printf("mixed32 negative_hole_reset_stable=PASS cyclic17_170_requests_17_scans=PASS negative_eviction_PS2_Wii_reader_faults=%zu recovery_same_hole=PASS\n",total_faults);
}
static void inherited(void){
 uint8_t raw[1024];FILE*f=tmpfile();assert(f);fixture(raw,0,0,1);assert(fwrite(raw,1,1024,f)==1024);fixture(raw,0,0,0);assert(fwrite(raw,1,1024,f)==1024);VolFile files[2]={{0},{0}};files[0].name="Child.bdp";files[0].size=1024;files[1].name="Present.bdp";files[1].size=1024;files[1].offset=1024;Vol v={0};v.f=f;v.files=files;v.count=2;
 City*a=fresh(&v),*b=fresh(&v);city_activate(a);LoadedBook*child=load_book(a,"Child");Group g=book_group(&child->book,"Root");Scene sa={0},sb={0};sa.owner=a;sb.owner=b;
 ledger_fail_read_name="Present.bdp";assert(group_num(g,"Answer",-99)==-99);assert(city_load_failed(a)&&scene_failed(&sa)&&!city_load_failed(b)&&!scene_failed(&sb));size_t calls=ledger_calls;assert(!people_template(a,"Root").at&&ledger_calls==calls);
 city_activate(b);LoadedBook*good=load_book(b,"Present");assert(good&&group_num(book_group(&good->book,"Root"),"Answer",0)==42);city_close(a);assert(active_city==b);city_close(b);assert(!active_city);fclose(f);assert(!ledger_live&&!ledger_bytes);
 puts("inherited_failure_latched=PASS nested_city_isolation=PASS inactive_close_preserves_resolver=PASS active_close_detaches=PASS");
}
static Group nested_city_callback(City*inner,City*outer,Scene*outer_scene){
 /* A synchronous nested callback selects another City, then explicitly restores its
  * caller's selection. The inner failure must never borrow/poison outer Scene. */
 city_activate(inner);Group g=city_book_group(inner,"Child","Root");ledger_fail_read_name="Present.bdp";assert(group_num(g,"Answer",-1)==-1&&city_load_failed(inner));assert(!scene_failed(outer_scene));city_activate(outer);return city_book_group(outer,"Child","Root");
}
static void nested_callbacks(void){
 uint8_t raw[1024];FILE*f=tmpfile();assert(f);fixture(raw,0,0,1);assert(fwrite(raw,1,1024,f)==1024);fixture(raw,0,0,0);assert(fwrite(raw,1,1024,f)==1024);VolFile rows[2]={{0}};rows[0].name="Child.bdp";rows[0].size=1024;rows[1].name="Present.bdp";rows[1].size=1024;rows[1].offset=1024;Vol v={0};v.f=f;v.files=rows;v.count=2;City*outer=fresh(&v),*inner=fresh(&v);Scene scene={0};scene.owner=outer;city_activate(outer);Group g=nested_city_callback(inner,outer,&scene);assert(g.at&&!city_load_failed(outer)&&!scene_failed(&scene)&&group_num(g,"Answer",-1)==42);size_t reads=ledger_read_calls;assert(!city_book_group(inner,"Present","Root").at&&ledger_read_calls==reads);city_close(inner);assert(active_city==outer);city_close(outer);fclose(f);assert(!ledger_bytes&&!ledger_live);puts("nested_callback_inner_failure_outer_healthy=PASS caller_selection_restored=PASS");
}
static void rename_fixture(uint8_t raw[1024],const char*root,const char*key,const char*value){
 const char*names[]={root,key,"ParentPropertySet",value};unsigned at=20;memset(raw+0x80,0,0x100);u32(raw+0x80,4,0);
 for(int i=0;i<4;i++){u32(raw+0x84+4*i,at,0);strcpy((char*)raw+0x80+at,names[i]);at+=(unsigned)strlen(names[i])+1;assert(at<0x100);}
}
static void open_contract(void){
 uint8_t raw[1024];FILE*f=tmpfile();assert(f);fixture(raw,0,0,0);assert(fwrite(raw,1,1024,f)==1024);fixture(raw,0,0,1);rename_fixture(raw,"WorldData","Answer","Root:Present");assert(fwrite(raw,1,1024,f)==1024);fixture(raw,0,0,0);assert(fwrite(raw,1,1024,f)==1024);
 VolFile rows[3]={{0}};const char*names[]={"Example.bdp","Example_Props.bdp","Present.bdp"};for(int i=0;i<3;i++){rows[i].name=names[i];rows[i].size=1024;rows[i].offset=1024*i;}Vol v={0};v.f=f;v.files=rows;v.count=3;
 City*prior=fresh(&v);city_activate(prior);size_t bytes=ledger_bytes,live=ledger_live;ledger_fail_read_name="Present.bdp";assert(!city_open(&v,"Example")&&active_city==prior&&ledger_bytes==bytes&&ledger_live==live);
 City*opened=city_open(&v,"Example");assert(opened&&active_city==opened);city_close(prior);assert(active_city==opened);city_close(opened);assert(!active_city);
 /* Reject original overlong template name before it can be silently truncated to absence. */
 char longname[80];memset(longname,'x',79);longname[79]=0;fixture(raw,0,0,0);rename_fixture(raw,"WorldData","ObjectTemplateBook",longname);raw[0x2d4]=PK_NAME;u32(raw+0x2dc,3,0);assert(!fseek(f,1024,SEEK_SET)&&fwrite(raw,1,1024,f)==1024);fflush(f);assert(!city_open(&v,"Example")&&!active_city);
 fclose(f);assert(!ledger_bytes&&!ledger_live);puts("city_open_inherited_failure_restore=PASS pretruncation_key_rejected=PASS");
}
static void twins(Vol*v,const char*path){
 FILE*f=plat_fopen(path,"rb");assert(f&&!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);fclose(f);VolFile wf={0};wf.name="Objects1_props.bdg";wf.path=path;wf.size=(uint32_t)size;v->wii=&wf;v->nwii=1;
 size_t bytes=ledger_bytes,live=ledger_live;City*c=fresh(v);Group g=book_or_twin(c,"Objects1_Props","ArmyTruck03");assert(g.at&&!g.book->b->wii);city_close(c);
 c=fresh(v);ledger_fail_read_name="Objects1_Props.bdp";size_t hits=ledger_fail_hits;g=book_or_twin(c,"Objects1_Props","ArmyTruck03");assert(!g.at&&city_load_failed(c)&&ledger_fail_hits==hits+1&&c->nbooks==0);size_t calls=ledger_calls;assert(!city_book_group_wii(c,"Objects1_Props","ArmyTruck03").at&&ledger_calls==calls);city_reset_load_failure(c);g=book_or_twin(c,"Objects1_Props","ArmyTruck03");assert(g.at&&!g.book->b->wii);Group keep=g;LoadedBook*twin=wii_twin(c,"Objects1_Props");assert(twin&&twin->b.wii&&keep.book->b==&c->books[0].b);city_close(c);assert(bytes==ledger_bytes&&live==ledger_live);v->wii=NULL;v->nwii=0;
 puts("PS2_precedence=PASS present_PS2_failure_no_Wii_fallback=PASS sticky_no_retry=PASS explicit_recovery_PS2=PASS borrowed_group_stable=PASS");
}
static void vol_cleanup(Vol*v){if(v->f)fclose(v->f);free(v->files);free(v->names);memset(v,0,sizeof*v);}
int main(int argc,char**argv){setvbuf(stdout,NULL,_IONBF,0);synthetic();mixed_slots();inherited();nested_callbacks();open_contract();if(argc>1){Vol v;assert(!vol_open(&v,argv[1]));sweep(&v,"Objects1_Props","real-ps2-objects");sweep(&v,"camera","real-ps2-camera");if(argc>2){FILE*f=plat_fopen(argv[2],"rb");assert(f&&!fseek(f,0,SEEK_END));long size=ftell(f);VolFile vf={0};vf.name="Objects1_props.bdg";vf.size=(uint32_t)size;Vol w={0};w.f=f;w.files=&vf;w.count=1;sweep(&w,"Objects1_props.bdg","real-wii-objects");fclose(f);twins(&v,argv[2]);}vol_cleanup(&v);}ledger_report("all_closed");assert(!ledger_live&&!ledger_bytes);return 0;}
