/* Type2DK direct ranging: 19--21 and 19--22 only.
 * No application data transfer. All UWB commands run on one app task. */
#ifdef MESH_HOST_TEST
#include "mock_sdk.h"
#else
#include "phUwb_BuildConfig.h"
#include "UwbApi.h"
#include "AppInternal.h"
#include "AppRecovery.h"
#include "Demo_SR040_RadioConfigAndGroupDelay.h"
#include "phOsalUwb_Thread.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fsl_debug_console.h"
#include "fsl_reset.h"
#endif
#include "range_utils.h"
#include <string.h>
#ifndef MESH_NODE
#error MESH_NODE must be 19,21,22
#endif
#define S_AB 0x19210001u
#define S_AC 0x19220001u
#define ADDR_A 0x1111u
#define ADDR_B 0x2222u
#define ADDR_C 0x3333u
#define COUNT(x) (sizeof(x)/sizeof((x)[0]))
typedef struct {uint16_t cm;uint32_t at;uint8_t valid,status;} edge_t;
typedef struct {uint32_t id,interval;uint16_t peer;uint8_t init,offset;} session_t;
#if MESH_NODE==19
#define LOCAL_ADDR ADDR_A
static const session_t sessions[]={{S_AB,200,ADDR_B,0,0},{S_AC,200,ADDR_C,0,0}};
#elif MESH_NODE==21
#define LOCAL_ADDR ADDR_B
static const session_t sessions[]={{S_AB,200,ADDR_A,1,0}};
#elif MESH_NODE==22
#define LOCAL_ADDR ADDR_C
static const session_t sessions[]={{S_AC,200,ADDR_A,1,60}};
#else
#error Bad MESH_NODE
#endif
static edge_t edges[2];
static volatile uint32_t range_ok,range_bad;
static volatile uint8_t session_state[2],session_reason[2];
static volatile uint8_t reset_seen, runtime_ready;
static volatile uint32_t heartbeat_ms,last_range[2];
static uint32_t last_attempt[2];
static uint8_t api_errors[2];
#if MESH_NODE==19
static uint32_t print_seq;
#endif
static uint32_t now_ms(void){return (uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);}
static int session_index(uint32_t id){for(unsigned i=0;i<COUNT(sessions);i++)if(sessions[i].id==id)return (int)i;return -1;}
static void on_range(const phRangingData_t *r)
{
    if(!r || session_index(r->sessionId)<0 || r->no_of_measurements>MAX_NUM_RESPONDERS)return;
    int si=session_index(r->sessionId);
    int e=r->sessionId==S_AB?0:1;
    uint16_t expected=sessions[si].peer;
    for(unsigned i=0;i<r->no_of_measurements;i++){
        const phRangingMesr_t *m=&r->ranging_meas.range_meas_twr[i];
        if(mesh_u16(m->mac_addr)!=expected)continue;
        taskENTER_CRITICAL();
        edges[e].status=m->status;
        if(m->status==UWBAPI_STATUS_OK && m->distance!=0xffff){
            last_range[si]=now_ms();
            edges[e].cm=m->distance;edges[e].at=now_ms();edges[e].valid=1;range_ok++;
        }else{range_bad++;} /* Keep the last good sample until its original time expires. */
        taskEXIT_CRITICAL();
    }
}
static void mesh_callback(eNotificationType type,void *data)
{
    if(type==UWBD_RANGING_DATA){on_range((const phRangingData_t*)data);return;}
    if(type==UWBD_DATA_RCV_NTF || type==UWBD_DATA_TRANSMIT_NTF)return;
    if(type==UWBD_SESSION_DATA && data){
        const phUwbSessionInfo_t *s=(const phUwbSessionInfo_t*)data;
        int i=session_index(s->session_id);if(i>=0){session_state[i]=s->state;session_reason[i]=s->reason_code;}
        return;
    }
    if(type==UWBD_DEVICE_RESET || type==UWBD_RECOVERY_NTF){reset_seen=1;return;}
    AppCallback(type,data);
}
static void expire_state(uint32_t now)
{
    /* Latch expiry so a disconnected value cannot become fresh at tick wrap. */
    taskENTER_CRITICAL();
    for(unsigned i=0;i<COUNT(edges);i++)
        if(!mesh_fresh(edges[i].valid,now,edges[i].at))edges[i].valid=0;
    taskEXIT_CRITICAL();
}
#if MESH_NODE==19
extern void *mLogMutex;
static void print_ranges(void)
{
    edge_t e[2];
    taskENTER_CRITICAL();memcpy(e,edges,sizeof(e));taskEXIT_CRITICAL();
    uint32_t t=now_ms();
    bool v0=mesh_fresh(e[0].valid,t,e[0].at),v1=mesh_fresh(e[1].valid,t,e[1].at);
    unsigned mask=(v0?1u:0u)|(v1?2u:0u);
    phOsalUwb_LockMutex(mLogMutex);
    PRINTF("RANGE,SEQ=%lu,MS=%lu,D19_21=%u,D19_22=%u,DV=%u,AGE21=%u,AGE22=%u\r\n",
        (unsigned long)++print_seq,(unsigned long)t,
        v0?e[0].cm:65535u,v1?e[1].cm:65535u,mask,
        v0?mesh_age16(t,e[0].at):65535u,v1?mesh_age16(t,e[1].at):65535u);
    phOsalUwb_UnlockMutex(mLogMutex);
}
#endif
static bool step_ok(const char *step,tUWBAPI_STATUS status,uint32_t sid){
    PRINTF("INIT,NODE=%d,STEP=%s,SID=%08lx,STATUS=%u\r\n",MESH_NODE,step,(unsigned long)sid,(unsigned)status);
    return status==UWBAPI_STATUS_OK;
}
static bool configure(const session_t *s)
{
    phRangingParams_t r={0},check={0};
    const UWB_AppParams_List_t cfg[]={
        UWB_SET_APP_PARAM_VALUE(RANGING_ROUND_USAGE,kUWB_RangingRoundUsage_DS_TWR),
        UWB_SET_APP_PARAM_VALUE(RFRAME_CONFIG,kUWB_RfFrameConfig_SP1),
        UWB_SET_APP_PARAM_VALUE(SLOTS_PER_RR,25),
        UWB_SET_APP_PARAM_VALUE(SLOT_DURATION,2400),
        UWB_SET_APP_PARAM_VALUE(RANGING_INTERVAL,s->interval),
        UWB_SET_APP_PARAM_VALUE(MAX_RR_RETRY,0),
        UWB_SET_APP_PARAM_VALUE(CHANNEL_NUMBER,5),
        UWB_SET_APP_PARAM_VALUE(SFD_ID,2),
        UWB_SET_APP_PARAM_VALUE(PREAMBLE_CODE_INDEX,10),
        UWB_SET_APP_PARAM_VALUE(PRF_MODE,kUWB_PrfMode_62_4MHz),
        UWB_SET_APP_PARAM_VALUE(TX_ADAPTIVE_PAYLOAD_POWER,1),
        UWB_SET_APP_PARAM_VALUE(AOA_RESULT_REQ,0),
        UWB_SET_APP_PARAM_VALUE(RNG_DATA_NTF,1),
        UWB_SET_APP_PARAM_VALUE(RANGING_START_OFFSET,s->offset),
    };
    if(!step_ok("SESSION",UwbApi_SessionInit(s->id,UWBD_RANGING_SESSION),s->id))return false;
    if(!step_ok("CONFIG",UwbApi_SetAppConfigMultipleParams(s->id,COUNT(cfg),cfg),s->id))return false;
    r.deviceRole=s->init?kUWB_DeviceRole_Initiator:kUWB_DeviceRole_Responder;
    r.deviceType=s->init?kUWB_DeviceType_Controller:kUWB_DeviceType_Controlee;
    r.multiNodeMode=kUWB_MultiNodeMode_UniCast;r.noOfControlees=1;r.macAddrMode=0;
    mesh_p16(r.deviceMacAddr,LOCAL_ADDR);mesh_p16(r.dstMacAddr,s->peer);
    if(!step_ok("PEERS",UwbApi_SetRangingParams(s->id,&r),s->id))return false;
    if(!step_ok("READBACK",UwbApi_GetRangingParams(s->id,&check),s->id))return false;
    if(check.deviceRole!=r.deviceRole || check.deviceType!=r.deviceType || check.multiNodeMode!=r.multiNodeMode ||
       check.noOfControlees!=1 || check.macAddrMode!=0 || memcmp(check.deviceMacAddr,r.deviceMacAddr,2) ||
       memcmp(check.dstMacAddr,r.dstMacAddr,2))return false;
    /* Read actual radio/timing values before START, not just our requested values. */
    const struct {eAppConfig id;uint32_t expected;const char *name;} verify[]={
        {SLOT_DURATION,2400,"SLOT"},{RANGING_INTERVAL,s->interval,"INTERVAL"},
        {SLOTS_PER_RR,25,"SLOTS"},{RFRAME_CONFIG,kUWB_RfFrameConfig_SP1,"RFRAME"},
        {SFD_ID,2,"SFD"},{RANGING_START_OFFSET,s->offset,"OFFSET"},
    };
    for(unsigned i=0;i<COUNT(verify);i++){
        uint32_t value=0;
        tUWBAPI_STATUS st=UwbApi_GetAppConfig(s->id,verify[i].id,&value);
        PRINTF("CONFIG,NODE=%d,SID=%08lx,PARAM=%s,VALUE=%lu,EXPECTED=%lu,STATUS=%u\r\n",
            MESH_NODE,(unsigned long)s->id,verify[i].name,(unsigned long)value,
            (unsigned long)verify[i].expected,(unsigned)st);
        if(st!=UWBAPI_STATUS_OK || value!=verify[i].expected)return false;
    }
    PRINTF("SESSION,NODE=%d,SID=%08lx,INIT=%u,SELF=%04x,PEER=%04x,INTERVAL_MS=%lu\r\n",
        MESH_NODE,(unsigned long)s->id,s->init,LOCAL_ADDR,s->peer,(unsigned long)s->interval);
    return true;
}
/* START rejection is session-local. Keep collecting and retry with backoff.
 * A successful command RSP alone does not mean that the session became Active. */
static bool start_session(unsigned i,const char *step)
{
    uint8_t state=UWBAPI_SESSION_ERROR;
    session_reason[i]=0xff; /* No reason notification received yet. */
    tUWBAPI_STATUS st=UwbApi_StartRangingSession(sessions[i].id);
    last_attempt[i]=now_ms();
    tUWBAPI_STATUS query=UwbApi_GetSessionState(sessions[i].id,&state);
    if(query!=UWBAPI_STATUS_OK){
        PRINTF("SESSION_WAIT,NODE=%d,SID=%08lx,QUERY=%u,STATUS=%u\r\n",
            MESH_NODE,(unsigned long)sessions[i].id,(unsigned)query,(unsigned)st);
        return ++api_errors[i]<3;
    }
    api_errors[i]=0;session_state[i]=state;
    if(state==UWBAPI_SESSION_ACTIVATED)session_reason[i]=0;
    PRINTF("START_RESULT,NODE=%d,SID=%08lx,STEP=%s,STATUS=%u,STATE=%u,REASON=%02x\r\n",
        MESH_NODE,(unsigned long)sessions[i].id,step,(unsigned)st,state,session_reason[i]);
    if(state==UWBAPI_SESSION_ACTIVATED){
        session_reason[i]=0;
        /* Grace period for peer synchronization, not a valid measurement. */
        last_range[i]=now_ms();
        return true;
    }
    if(state==UWBAPI_SESSION_IDLE){
        PRINTF("SESSION_WAIT,NODE=%d,SID=%08lx,REASON=%02x,ACTION=RETRY_NO_REBOOT\r\n",
            MESH_NODE,(unsigned long)sessions[i].id,session_reason[i]);
        return true;
    }
    return false; /* Missing/corrupt session needs full reinitialization. */
}
static bool recover_sessions(void)
{
    for(unsigned i=0;i<COUNT(sessions);i++){
        uint8_t state=UWBAPI_SESSION_ERROR;
        tUWBAPI_STATUS st=UwbApi_GetSessionState(sessions[i].id,&state);
        if(st!=UWBAPI_STATUS_OK){if(++api_errors[i]>=3)return false;continue;}
        api_errors[i]=0;session_state[i]=state;
        uint32_t t=now_ms(),interval=12000u+MESH_NODE*137u+i*1009u;
        bool stale=mesh_retry_due(t,last_range[i],last_attempt[i],interval);
        if(state==UWBAPI_SESSION_ACTIVATED && stale){
            PRINTF("RECOVER,NODE=%d,SID=%08lx,REASON=NO_PROGRESS,ACTION=RESTART_SESSION\r\n",MESH_NODE,(unsigned long)sessions[i].id);
            last_attempt[i]=t;
            st=UwbApi_StopRangingSession(sessions[i].id);
            if(st!=UWBAPI_STATUS_OK)continue;
            if(!start_session(i,"RESTART"))return false;
        }else if(state==UWBAPI_SESSION_IDLE){
            uint32_t retry_ms=5000u+MESH_NODE*53u+i*211u;
            if((uint32_t)(t-last_attempt[i])>=retry_ms && !start_session(i,"RETRY"))return false;
        }else if(state!=UWBAPI_SESSION_ACTIVATED){return false;}
    }
    return true;
}
static OSAL_TASK_RETURN_TYPE mesh_guard(void *unused)
{
    (void)unused;
    for(;;){
        phOsalUwb_Delay(250);
        /* Separate task: recover even if the application is blocked in an API/mutex.
         * This software guard requires the RTOS scheduler/interrupts to be alive. */
        uint32_t limit=runtime_ready?45000u:180000u;
        if((uint32_t)(now_ms()-heartbeat_ms)>limit)RESET_SystemReset();
    }
}
static OSAL_TASK_RETURN_TYPE mesh_task(void *unused)
{
    (void)unused;
    heartbeat_ms=now_ms();
    for(unsigned i=0;i<COUNT(edges);i++){edges[i].cm=0xffff;edges[i].at=now_ms()-65535u;}
    PRINTF("BOOT,TYPE2DK_DIRECT_RANGE_V1,NODE=%d,ADDR=%04x,UART=3000000\r\n",MESH_NODE,LOCAL_ADDR);
    if(!step_ok("UWB",UwbApi_Init(mesh_callback),0))goto fail;
    if(!step_ok("RADIO",RadioConfigFull_GroupDelay(FALSE),0))goto fail;
    if(!step_ok("SWUP",demo_sr040_swup_update_safe(),0))goto fail;
    reset_seen=0;
    for(unsigned i=0;i<COUNT(sessions);i++)if(!configure(&sessions[i]))goto fail;
    for(unsigned i=0;i<COUNT(sessions);i++){
        if(!start_session(i,"START"))goto fail;
    }
    runtime_ready=1;
    uint32_t health_at=now_ms();
#if MESH_NODE==19
    uint32_t print_at=0;
#endif
    for(;;){
        uint32_t t=now_ms();
        heartbeat_ms=t;
        expire_state(t);
        if(reset_seen)goto fail;
#if MESH_NODE==19
        if((uint32_t)(t-print_at)>=100){print_ranges();print_at=t;}
#endif
        if((uint32_t)(now_ms()-health_at)>=2000){
            health_at=now_ms();
            PRINTF("HEALTH,NODE=%d,R_OK=%lu,R_BAD=%lu,SESSIONS=%u,S=%u/%u,WHY=%02x/%02x,HEAP=%u\r\n",
                MESH_NODE,(unsigned long)range_ok,(unsigned long)range_bad,(unsigned)COUNT(sessions),
                session_state[0],session_state[1],session_reason[0],session_reason[1],(unsigned)xPortGetFreeHeapSize());
            if(!recover_sessions())goto fail;
        }
        phOsalUwb_Delay(10);
    }
fail:
    taskENTER_CRITICAL();
    for(unsigned i=0;i<COUNT(edges);i++)edges[i].valid=0;
    taskEXIT_CRITICAL();
    PRINTF("RECOVER,NODE=%d,REASON=INIT_OR_API_ERROR,ACTION=AUTO_REBOOT\r\n",MESH_NODE);
    phOsalUwb_Delay(3000u+MESH_NODE*53u);
    RESET_SystemReset();
    for(;;){}
}
UWBOSAL_TASK_HANDLE uwb_demo_start(void)
{
    phOsalUwb_ThreadCreationParams_t p={0};UWBOSAL_TASK_HANDLE h=NULL;
    heartbeat_ms=now_ms();runtime_ready=0;
    UWBOSAL_TASK_HANDLE guard=NULL;
    p.stackdepth=512;PHOSALUWB_SET_TASKNAME(p,"RangeGuard");p.priority=5;p.pContext=NULL;
    if(phOsalUwb_Thread_Create((void**)&guard,mesh_guard,&p)!=0)RESET_SystemReset();
    p.stackdepth=2048;PHOSALUWB_SET_TASKNAME(p,"Type2DKRange");p.priority=4;p.pContext=NULL;
    if(phOsalUwb_Thread_Create((void**)&h,mesh_task,&p)!=0)RESET_SystemReset();
    return h;
}
