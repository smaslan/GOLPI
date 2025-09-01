//------------------------------------------------------------------------------
// Script for transfering variables to Octave environment via named pipes.
//
// Data format to be send by caller:
//   DWORD - variable_type_id
//   DWORD - rows_count
//   DWORD - columns_count
//   BYTES - variable data
// 
// Usage:
//   [var_name] = golpi_pipe_receive(pipe_name)
//   [var_name] = golpi_pipe_receive(pipe_name, timeout)
//
// Parameters:
//   pipe_name: Windows named pipe that has to be created by caller beforehand
//              e.g. '\\.\Pipe\GOLPI_data_pipe'
//   timeout: Total data read timeout value [s] (optional)
// 
// (c) 2025, Stanislav Maslan, smaslan@cmi.cz
//------------------------------------------------------------------------------
//#include <octave/oct.h>
//#include <windows.h>

// variable type IDs
#define VTYPE_ERROR 0 /* unknown type */
#define VTYPE_STRING 1 /* simple string */
#define VTYPE_INT8 2 /* 8bit signed integer */
#define VTYPE_UINT8 3 /* 8bit unsigned integer */
#define VTYPE_INT16 4 /* 16bit signed integer */
#define VTYPE_UINT16 5 /* 16bit unsigned integer */
#define VTYPE_INT32 6 /* 32bit signed integer */
#define VTYPE_UINT32 7 /* 32bit unsigned integer */
#define VTYPE_DBL 8 /* 64bit float */
#define VTYPE_CDBL 9 /* 64bit complex float (re,im,re,im, ...) */
#define VTYPE_SGL 10 /* 32bit float */
#define VTYPE_CSGL 11 /* 32bit complex float (re,im,re,im, ...) */


// enable some debug prints
#define DEBUG_PRN 0

#define min(a,b) (((a)<(b))?(a):(b))

// --- Timer stuff ---
typedef struct{
    LARGE_INTEGER freq;
  	LARGE_INTEGER t_ref;
}TTimer;

// init interval timer
void timer_init(TTimer *timer);

// get elapsed time from timer_init() in seconds
double timer_get(TTimer *timer);

// read file with timeout
DWORD ReadFileTimeout(HANDLE file, LPVOID data, DWORD size, DWORD *read_bytes, double timeout);
DWORD WriteFileTimeout(HANDLE file, LPVOID data, DWORD size, DWORD *written_bytes, DWORD block_size, double timeout);
DWORD WriteFileTimeoutACK(HANDLE file, LPVOID data, DWORD size, DWORD *written_bytes, DWORD block_size, double total_timeout);