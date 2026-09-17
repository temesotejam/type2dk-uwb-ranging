/* Executes the actual application loop against a simulated UWB boundary.
 * This proves host recovery/control flow, not RF interoperability. */
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#define MESH_HOST_TEST 1
#include "../../source/direct_range.c"
static uint32_t tick,limit,release_at;
static uint8_t hwstate[2];
static unsigned starts[2],stops[2],boots,meshes,resets,query_fail;
static unsigned feed_ranges;
static uint32_t configs[2][15];
static phRangingParams_t params[2];
static jmp_buf done;
static char last_mesh[600];
void *mLogMutex;
uint32_t xTaskGetTickCount(void){return tick;}
size_t xPortGetFreeHeapSize(void){return 10000;}
int mock_printf(const char *fmt,...){
    if(!strncmp(fmt,"BOOT,",5))boots++;
    if(!strncmp(fmt,"RANGE,",5)){
        meshes++;va_list args;va_start(args,fmt);vsnprintf(last_mesh,sizeof(last_mesh),fmt,args);va_end(args);
    }
    return 0;
}
void phOsalUwb_LockMutex(void *p){(void)p;}
void phOsalUwb_UnlockMutex(void *p){(void)p;}
void phOsalUwb_Delay(uint32_t ms){
    tick+=ms;
    for(unsigned i=0;i<COUNT(sessions);i++)if((feed_ranges&(1u<<i)) && hwstate[i]==2){
        phRangingData_t r={0};r.sessionId=sessions[i].id;r.no_of_measurements=1;
        mesh_p16(r.ranging_meas.range_meas_twr[0].mac_addr,sessions[i].peer);
        r.ranging_meas.range_meas_twr[0].distance=123;
        mesh_callback(UWBD_RANGING_DATA,&r);
    }
    if(limit && tick>=limit)longjmp(done,1);
}
void RESET_SystemReset(void){resets++;longjmp(done,2);}
int RNG_Init(void){return 0;}
int RNG_HwGetRandomNo(uint32_t *p){*p=0x11223344;return 0;}
int phOsalUwb_Thread_Create(void **a,void (*b)(void *),void *c){(void)a;(void)b;(void)c;return 0;}
void AppCallback(eNotificationType t,void *p){(void)t;(void)p;}
tUWBAPI_STATUS RadioConfigFull_GroupDelay(bool b){(void)b;return 0;}
tUWBAPI_STATUS demo_sr040_swup_update_safe(void){return 0;}
tUWBAPI_STATUS UwbApi_Init(void (*cb)(eNotificationType,void *)){(void)cb;return 0;}
bool mesh_accel_init(uint8_t *who){*who=0x84;return true;}
bool mesh_accel_sample(int16_t v[3]){v[0]=0;v[1]=0;v[2]=1000;return true;}
tUWBAPI_STATUS UwbApi_SessionInit(uint32_t sid,unsigned type){(void)type;hwstate[session_index(sid)]=3;return 0;}
tUWBAPI_STATUS UwbApi_SetAppConfigMultipleParams(uint32_t sid,unsigned n,const UWB_AppParams_List_t *p){for(unsigned j=0;j<n;j++)configs[session_index(sid)][p[j].id]=p[j].value;return 0;}
tUWBAPI_STATUS UwbApi_GetAppConfig(uint32_t sid,eAppConfig id,uint32_t *v){*v=configs[session_index(sid)][id];return 0;}
tUWBAPI_STATUS UwbApi_SetRangingParams(uint32_t sid,phRangingParams_t *p){params[session_index(sid)]=*p;return 0;}
tUWBAPI_STATUS UwbApi_GetRangingParams(uint32_t sid,phRangingParams_t *p){*p=params[session_index(sid)];return 0;}
tUWBAPI_STATUS UwbApi_StartRangingSession(uint32_t sid){
    int i=session_index(sid);starts[i]++;
    /* Recorded trace: second START accepted as a command, then idle/reason20. */
    bool reject=i==1 && tick<release_at;
    hwstate[i]=reject?3:2;
    phUwbSessionInfo_t s={sid,hwstate[i],reject?0x20:0};
    mesh_callback(UWBD_SESSION_DATA,&s);
    return reject?2:0;
}
tUWBAPI_STATUS UwbApi_StopRangingSession(uint32_t sid){int i=session_index(sid);stops[i]++;hwstate[i]=3;return 0;}
tUWBAPI_STATUS UwbApi_GetSessionState(uint32_t sid,uint8_t *p){if(query_fail)return 2;*p=hwstate[session_index(sid)];return 0;}
tUWBAPI_STATUS UwbApi_SendData(phUwbDataPkt_t *p){(void)p;assert(!"Data transfer must never run");return 2;}

#if MESH_NODE==19
#include "replay_v2_log.h"
static void test_replay_and_stale(void)
{
    memset(edges,0,sizeof(edges));
    for(unsigned i=0;i<COUNT(edges);i++){edges[i].cm=0xffff;edges[i].at=0-65535u;}
    range_ok=range_bad=0;
    unsigned visible=0;
    for(unsigned i=0;i<COUNT(replay);i++){
        tick=replay[i].ms;
        if(!replay[i].kind){
            phRangingData_t r={0};r.sessionId=replay[i].sid;r.no_of_measurements=1;
            phRangingMesr_t *m=&r.ranging_meas.range_meas_twr[0];
            mesh_p16(m->mac_addr,replay[i].peer);m->status=replay[i].status;m->distance=replay[i].cm;
            mesh_callback(UWBD_RANGING_DATA,&r);
        }else{
            expire_state(tick);print_ranges();
            if(strstr(last_mesh,",DV=3,"))visible++;
        }
    }
    assert(range_ok==35 && range_bad==25);
    assert(visible==71 && edges[0].cm==18 && edges[1].cm==41);
    printf("PASS node19: recorded v2 log,35 good/25 bad ranges,71/88 rows with both distances valid\n");
    /* Bad MAC or unconfigured21--22 session cannot refresh or replace samples. */
    edge_t original[2];memcpy(original,edges,sizeof(edges));
    phRangingData_t r={0};r.sessionId=S_AB;r.no_of_measurements=1;
    mesh_p16(r.ranging_meas.range_meas_twr[0].mac_addr,ADDR_C);
    r.ranging_meas.range_meas_twr[0].distance=777;
    on_range(&r);r.sessionId=0x21220001u;on_range(&r);
    assert(!memcmp(original,edges,sizeof(edges)));
    tick+=3001;expire_state(tick);print_ranges();
    assert(strstr(last_mesh,"D19_21=65535,D19_22=65535,DV=0,AGE21=65535,AGE22=65535"));
    /* Expiry remains latched even if the millisecond clock wraps to an old time. */
    tick=original[0].at;expire_state(tick);print_ranges();assert(strstr(last_mesh,",DV=0,"));
    puts("PASS node19: sender/session filtering,3s stale invalidation,invalid output sentinel,latched expiry");
}
#endif
int main(void)
{
    assert(COUNT(sessions)==(MESH_NODE==19?2:1));
    for(unsigned i=0;i<COUNT(sessions);i++)assert(sessions[i].id==S_AB || sessions[i].id==S_AC);
    tick=100;limit=360100;release_at=120100;feed_ranges=3;
    int why=setjmp(done);
    if(!why)mesh_task(NULL);
    assert(why==1 && boots==1 && resets==0 && runtime_ready);
    assert(starts[0]==1 && stops[0]==0 && hwstate[0]==2);
#if MESH_NODE==19
    assert(starts[1]>1 && starts[1]<30 && stops[1]==0 && hwstate[1]==2 && meshes>3000);
    puts("PASS node19: 360s loop,second-session start rejection for120s,other link/output preserved,later recovery");
#else
    assert(starts[1]==0 && stops[1]==0);
    printf("PASS node%d: one direct session,360s loop,no reboot,no application data sending\n",MESH_NODE);
#endif
    for(unsigned i=0;i<COUNT(sessions);i++)assert(configs[i][RANGING_INTERVAL]==200);
    /* Peer0 goes away for20s. Peer1 keeps producing measurements if present. */
    limit=0;feed_ranges=2;
    uint32_t until=tick+20000;
    unsigned start0=starts[0],stop0=stops[0],start1=starts[1],stop1=stops[1];
    while(tick<until){phOsalUwb_Delay(2000);expire_state(tick);assert(recover_sessions());}
    assert(starts[0]>start0 && stops[0]>stop0);
    assert(starts[1]==start1 && stops[1]==stop1);
    unsigned edge=MESH_NODE==22?1:0;
    assert(!edges[edge].valid);
#if MESH_NODE==19
    print_ranges();assert(strstr(last_mesh,"D19_21=65535,D19_22=123,DV=2,"));
#endif
    /* Peer0 returns without user input or resetting the collector. */
    feed_ranges=3;phOsalUwb_Delay(200);expire_state(tick);
    assert(edges[edge].valid && edges[edge].cm==123 && resets==0);
#if MESH_NODE==19
    print_ranges();assert(strstr(last_mesh,",DV=3,"));
#endif
    printf("PASS node%d:20s peer disappearance,session retry,unaffected peer preserved,automatic reacquisition\n",MESH_NODE);
    /* Retry deadline crosses uint32 millisecond wrap. */
    release_at=0;feed_ranges=0;hwstate[0]=3;last_attempt[0]=UINT32_MAX-1000u;tick=1000;
    if(COUNT(sessions)>1)last_range[1]=last_attempt[1]=tick;
    unsigned n=starts[0];assert(recover_sessions());assert(starts[0]==n);
    tick=7000;assert(recover_sessions());assert(starts[0]==n+1);
    query_fail=1;assert(recover_sessions());assert(recover_sessions());assert(!recover_sessions());
    printf("PASS node%d:tick-wrap retry timing,three transport failures escalate recovery\n",MESH_NODE);
#if MESH_NODE==19
    test_replay_and_stale();
#endif
    return 0;
}
