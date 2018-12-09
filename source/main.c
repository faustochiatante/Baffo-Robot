#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "ev3.h"
#include "ev3_port.h"
#include "ev3_tacho.h"
#include "ev3_sensor.h"
#include <pthread.h>
// WIN32 /////////////////////////////////////////
#ifdef __WIN32__

#include <windows.h>

// UNIX //////////////////////////////////////////
#else

#include <unistd.h>
#define Sleep( msec ) usleep(( msec ) * 1000 )

//////////////////////////////////////////////////
#endif
const char const *color[] = {"?", "BLACK", "BLUE", "GREEN", "YELLOW", "RED", "WHITE", "BROWN" };

/****************************************************************************************************/
/*                                        DEFINE                                                    */
/****************************************************************************************************/

#define COLOR_COUNT  (( int )( sizeof( color ) / sizeof( color[ 0 ])))
#define PI 3.141592653589793
#define DIAM 5.6
#define RAD 2.8
#define CF_RAMP_UP 2/3
#define CF_RAMP_DW 2/3
#define MAX_SPEED 1050
#define AXE_WHEELS 12
#define ROT_ADJ 3/5
#define ROBOT_WIDTH 12
#define ROBOT_LENGTH 30
#define ROBOT_SENSOR_DIST 6
#define FIELD_WIDTH 120 - ROBOT_WIDTH/2
#define FIELD_LENGTH 100 - ROBOT_LENGTH/2
#define START_POS_X 0
#define START_POS_Y 0
#define ANGLE_X FIELD_WIDTH/2-ROBOT_SENSOR_DIST
#define ANGLE_Y FIELD_LENGTH-10-ROBOT_SENSOR_DIST-ROBOT_LENGTH/2

/****************************************************************************************************/
/*                                   GLOBAL VARIABLES                                               */
/****************************************************************************************************/
struct Position {
  float x;
  float y;
  int deg;
  int start_deg;
};

struct CornerAngles {
  int bl; //bottom left
  int tl; //top right
  int tr;
  int br;
};

struct Position pos;
uint8_t sn_tacho[2];  //2 tacho motors
uint8_t sn_ball;		  //tacho to throw the ball
uint8_t sn_lift;      //tacho to lift the ball
uint8_t sn_gyro;      //gyroscope
uint8_t sn_us;        //ultrasonic distance sensor
uint8_t sn_touch;
uint8_t sn_color;
uint8_t sn_sonar;
uint8_t sn_mag;

FLAGS_T state;
char s[ 256 ];
int val;
float value;

pthread_mutex_t sem_gyro;
pthread_mutex_t sem_us;

volatile int gyro_dir = 0.0;
volatile int us_dist = 0.0;
volatile int flag_kill = 0;

/****************************************************************************************************/
/*                                  FUNCTION PROTOTYPES                                             */
/****************************************************************************************************/

void sensors_init(void);
void rotate(int deg, uint8_t * sn);
void rotate_action(int deg, uint8_t * sn);
void go_straight_cm(int cm, uint8_t * sn);
void tacho_wait_term(uint8_t motor);
void tacho_wait_ball(uint8_t motor);
float turn_speed(int deg);
float read_gyro(uint8_t sn_gyro);
int read_us(uint8_t sn_us);
void* gyro_thread(void* arg);
void* us_thread(void* arg);
void throwball(uint8_t sn, int divisionfactor);
void liftball(uint8_t sn);
void update_corner_angles(struct CornerAngles *c_angles, struct Position pos);
void update_position(int movement, int degree_abs);

/****************************************************************************************************/
/****************************************************************************************************/

static bool _check_pressed( uint8_t sn )
{
  int val;

  if ( sn == SENSOR__NONE_ ) {
    return ( ev3_read_keys(( uint8_t *) &val ) && ( val & EV3_KEY_UP ));
  }
  return ( get_sensor_value( 0, sn, &val ) && ( val != 0 ));
}


void tacho_wait_term(uint8_t motor) {
	FLAGS_T state;
	do {
		get_tacho_state_flags(motor, &state);
    //printf("State: %d\n", state);
	} while (state);
}

void tacho_wait_ball(uint8_t motor) {
	FLAGS_T state;
	do {
		get_tacho_state_flags(motor, &state);
    //printf("State: %d\n", state);
	} while (state!=2);
}

void go_straight_cm(int cm, uint8_t * sn) {
	int max_speed[2];
	// set the relative rad displacement in order to reach the correct displacement in cm
	float deg = 360 * cm / (PI * DIAM);

	get_tacho_max_speed(sn[0], &max_speed[0]);
	//get_tacho_max_speed(sn[1], &max_speed[1]);
	// change the braking mode
	multi_set_tacho_stop_action_inx(sn, TACHO_BRAKE);
	// set the max speed
	multi_set_tacho_speed_sp(sn, MAX_SPEED/5);
	// set ramp up & down speed
	multi_set_tacho_ramp_up_sp(sn, MAX_SPEED*CF_RAMP_UP);
	multi_set_tacho_ramp_down_sp(sn, MAX_SPEED*CF_RAMP_DW);
	// set the disp on the motors
	multi_set_tacho_position_sp(sn, deg);
	// initialize the tacho
  set_tacho_command_inx(sn[0], TACHO_RUN_TO_REL_POS);
  set_tacho_command_inx(sn[1], TACHO_RUN_TO_REL_POS);
	tacho_wait_term(sn[0]);
	tacho_wait_term(sn[1]);
  update_position(cm, 0);
}
void rotate(int deg, uint8_t * sn) {
  rotate_action(deg, sn);
  update_position(0, deg);
  return;
}

void rotate_action(int deg, uint8_t * sn) {
  int initial_rot, end_rot;
	float degree = AXE_WHEELS*(1.0*deg) / DIAM;

	multi_set_tacho_stop_action_inx(sn, TACHO_BRAKE);
	// set ramp up & down speed at zero
	multi_set_tacho_ramp_up_sp(sn, 0);
	multi_set_tacho_ramp_down_sp(sn,0);
	multi_set_tacho_speed_sp(sn, turn_speed(deg));

	// set the disp on the motors
	set_tacho_position_sp(sn[0], (int)(-0.9*degree));
	set_tacho_position_sp(sn[1], (int)(0.9*degree));

	// initialize the tacho
  initial_rot = gyro_dir;
	set_tacho_command_inx(sn[0], TACHO_RUN_TO_REL_POS);
  set_tacho_command_inx(sn[1], TACHO_RUN_TO_REL_POS);
	tacho_wait_term(sn[0]);
	tacho_wait_term(sn[1]);
  Sleep(200);
  pthread_mutex_lock(&sem_gyro);
  end_rot = gyro_dir;
  pthread_mutex_unlock(&sem_gyro);
  //printf("started at: %d  --- ended at: %d  --- rotate deg: %d\n",initial_rot,end_rot,deg);
  //fflush(stdout);
  if ((end_rot > initial_rot && deg>0) || (end_rot < initial_rot && deg<0)){
    rotate((initial_rot-end_rot+deg), sn);
  }
  else if (end_rot < initial_rot  && deg>0){
    rotate((initial_rot-end_rot+deg-360), sn);
  }else if(end_rot > initial_rot && deg<0){
    rotate((initial_rot-end_rot+deg+360), sn);
  }
  return;
}

float turn_speed(int deg){
  if(deg<0) deg=-deg;
  if(deg>90){
    return MAX_SPEED*ROT_ADJ*1/9;
  }else if(deg>20){
    return MAX_SPEED*ROT_ADJ/10;
  } else {
    return MAX_SPEED*ROT_ADJ/10;
  }
}

// return a float but use an integer to store the value,
// mantissa is always zero
float read_gyro (uint8_t sn_gyro){
	float value;

	if (!get_sensor_value0(sn_gyro, &value)){
		return 0;
	}
  return value;
}


void *gyro_thread(void* arg){
 	//Sleep(10);
 	uint8_t* gyro = (uint8_t*) arg;
 	printf("T1: started gyro thread: %d\n", *gyro);
 	while(flag_kill==0) {
 		pthread_mutex_lock(&sem_gyro);
 		gyro_dir = ((((int)(read_gyro(*gyro)) % 360) + 360) % 360);
 		pthread_mutex_unlock(&sem_gyro);
 	}
 	pthread_exit(0);
}

int read_us(uint8_t sn_us){
  int value;

  if (!get_sensor_value(0, sn_us, &value)){
		return 0;
	}
  return value;
}

void *us_thread(void* arg){
 	uint8_t* us = (uint8_t*) arg;
 	printf("T1: started gyro thread: %d\n", *us);
 	while(flag_kill==0) {
 		pthread_mutex_lock(&sem_us);
 		us_dist = read_us(*us);
 		pthread_mutex_unlock(&sem_us);
 	}
 	pthread_exit(0);
}

void throwball(uint8_t sn, int divisionfactor) {
	int deg = 65;
  int max_speed;
	// change the braking mode
  get_tacho_max_speed(sn, &max_speed);
  set_tacho_stop_action_inx(sn, TACHO_BRAKE);
	// set the max speed
	set_tacho_speed_sp(sn, max_speed/divisionfactor);
	// set ramp up & down speed
	set_tacho_ramp_up_sp(sn, max_speed/divisionfactor/2*CF_RAMP_UP);
	set_tacho_ramp_down_sp(sn, CF_RAMP_DW);
	// set the disp on the motors
	set_tacho_position_sp(sn, deg);
  set_tacho_command_inx(sn, TACHO_RUN_TO_REL_POS);
  tacho_wait_term(sn);
  //return to initial position
  set_tacho_stop_action_inx(sn, TACHO_BRAKE);
	// set the max speed
	set_tacho_speed_sp(sn, max_speed);
	// set ramp up & down speed
	set_tacho_ramp_up_sp(sn, max_speed*CF_RAMP_UP);
	set_tacho_ramp_down_sp(sn, 0);
	// set the disp on the motors
	set_tacho_position_sp(sn, -deg);
  set_tacho_command_inx(sn, TACHO_RUN_TO_REL_POS);
}

void liftball(uint8_t sn) {
	int deg = -360;
  int max_speed;
	// change the braking mode
  get_tacho_max_speed(sn, &max_speed);
  set_tacho_stop_action_inx(sn, TACHO_BRAKE);
	// set the max speed
	set_tacho_speed_sp(sn, max_speed);
	// set ramp up & down speed
	set_tacho_ramp_up_sp(sn, max_speed*CF_RAMP_UP);
	set_tacho_ramp_down_sp(sn, 0);
	// set the disp on the motors
	set_tacho_position_sp(sn, deg);
  set_tacho_command_inx(sn, TACHO_RUN_TO_REL_POS);
  tacho_wait_ball(sn);
  //return to abs pos
  set_tacho_position_sp(sn, 0);
  set_tacho_speed_sp(sn, max_speed/3);
  set_tacho_ramp_up_sp(sn, max_speed/3*CF_RAMP_UP);
	set_tacho_ramp_down_sp(sn, max_speed/3*CF_RAMP_UP);
  set_tacho_command_inx(sn, TACHO_RUN_TO_ABS_POS);
}

void sensors_init(){
	printf( "Sensors_init: Waiting tacho is plugged...\n" );

  //Tacho Motors Initialization
  while ( ev3_tacho_init() < 4 ) Sleep( 1000 );
  ev3_search_tacho_plugged_in(65,0, &sn_tacho[0], 0);
  ev3_search_tacho_plugged_in(66,0, &sn_lift, 0);
	ev3_search_tacho_plugged_in(67,0, &sn_ball, 0);
  ev3_search_tacho_plugged_in(68,0, &sn_tacho[1], 0);


  //Sensors Initialization
  ev3_sensor_init();
	ev3_search_sensor(LEGO_EV3_GYRO, &sn_gyro,0);
  ev3_search_sensor(LEGO_EV3_US, &sn_us,0);
	printf("Sensors_init: gyro ID %d\n",sn_gyro);
	set_sensor_mode_inx(sn_gyro, GYRO_GYRO_ANG);
}

void update_corner_angles(struct CornerAngles *c_angles, struct Position pos){
  c_angles->bl=-90-180/PI*atan((ANGLE_Y+FIELD_LENGTH+10.0+pos.y)/(ANGLE_X+pos.x));
  c_angles->tl=180/PI*-atan((ANGLE_X+pos.x)/(ANGLE_Y-pos.y));
  c_angles->tr=180/PI*atan((ANGLE_X-pos.x)/(ANGLE_Y-pos.y));
  c_angles->br=90+180/PI*atan((ANGLE_Y+FIELD_LENGTH+10.0+pos.y)/(ANGLE_X-pos.x));
  return;
}

void update_position(int movement, int degree){
  pos.deg=(pos.deg+degree+360)%360;
  pos.x+=movement*sin(PI*pos.deg/180);
  pos.y+=movement*cos(PI*pos.deg/180);
  printf("Position x:%.2f y:%.2f deg:%d deg_abs:%d\n", pos.x, pos.y, pos.deg, pos.start_deg);
}

/*****************************************MAIN**********************************************/

int main( void ) {
  struct CornerAngles c_angles;
  pthread_t thread[2];
  int t_ret1, t_ret2;
  int i;

#ifndef __ARM_ARCH_4T__
  /* Disable auto-detection of the brick (you have to set the correct address below) */
  //ev3_brick_addr = "192.168.0.204";
#endif

  if ( ev3_init() == -1 ) return ( 1 );

#ifndef __ARM_ARCH_4T__
  //printf( "The EV3 brick auto-detection is DISABLED,\nwaiting %s online with plugged tacho...\n", ev3_brick_addr );
#else
#endif

  //initialize sensors
	sensors_init();
  printf("In main\n");

	//Gyroscope and US Sensor Thread and Mutex creations
  pthread_mutex_init(&sem_gyro, NULL);
  pthread_mutex_init(&sem_us, NULL);

  t_ret1=pthread_create(&thread[1], NULL, &gyro_thread, (void*)(&sn_gyro));
  t_ret2=pthread_create(&thread[2], NULL, &us_thread, (void*)(&sn_us));

  if(t_ret1) {
    fprintf(stderr,"Error - pthread_create() gyro_thread return code: %d\n", t_ret1);
    exit(EXIT_FAILURE);
  }
  if(t_ret2) {
    fprintf(stderr,"Error - pthread_create() us_thread return code: %d\n", t_ret1);
    exit(EXIT_FAILURE);
  }

  //Initial setup
  Sleep(1000);
  pos.x=START_POS_X;
  pos.y=START_POS_Y;
  pthread_mutex_lock(&sem_gyro);
  pos.start_deg=gyro_dir;                 //Not guaranteed that gyro_dir has the right value
  pthread_mutex_unlock(&sem_gyro);
  pos.deg=0;
/*
  go_straight_cm(10, sn_tacho);
  liftball(sn_lift);
  Sleep(1000);
	throwball(sn_ball, 2);
*/
  printf("Position x:%.2f y:%.2f deg:%d deg_abs:%d\n", pos.x, pos.y, pos.deg, pos.start_deg);
  update_corner_angles(&c_angles, pos);
  printf("bl:%d\ntl:%d\ntr:%d\nbr:%d\n", c_angles.bl, c_angles.tl, c_angles.tr, c_angles.br);
  rotate(c_angles.bl, sn_tacho);
  Sleep(3000);
  //rotate(-c_angles.bl, sn_tacho);
  go_straight_cm(25, sn_tacho);
  update_corner_angles(&c_angles, pos);
  printf("bl:%d\ntl:%d\ntr:%d\nbr:%d\n", c_angles.bl, c_angles.tl, c_angles.tr, c_angles.br);
  printf("Position x:%.2f y:%.2f deg:%d deg_abs:%d\n", pos.x, pos.y, pos.deg, pos.start_deg);
  /*
  for(;;){
    pthread_mutex_lock(&sem_us);
    printf("%d\n", us_dist);
    pthread_mutex_unlock(&sem_us);
    Sleep(100);
  }
  */
  flag_kill = 1;

  pthread_join(thread[1],NULL);
  pthread_mutex_destroy(&sem_gyro);
  pthread_mutex_destroy(&sem_us);

  pthread_cancel(thread[1]);

  //ev3_uninit();

  printf( "*** ( EV3 ) Bye! ***\n" );

  return ( 0 );
}
