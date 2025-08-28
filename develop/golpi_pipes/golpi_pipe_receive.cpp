//#include <iostream>
//#include <cstdlib>
//#include <stdio.h>
//#include <math.h>
#include <string.h>

#include <octave/oct.h>
#include <windows.h>

//#include "mex.h"
//#include "matrix.h"


#define VTYPE_STRING 0
#define VTYPE_INT32 1
#define VTYPE_UINT32 2
#define VTYPE_DBL 3
#define VTYPE_CDBL 4
#define VTYPE_SGL 5
#define VTYPE_CSGL 6



#define DEBUG_PRN 0


// --- Timer stuff ---
typedef struct{
    LARGE_INTEGER freq;
  	LARGE_INTEGER t_ref;
}TTimer;

// init interval timer
void timer_init(TTimer *timer)
{
    QueryPerformanceFrequency(&timer->freq);
    QueryPerformanceCounter(&timer->t_ref);  
}

// get elapsed time from timer_init() in seconds
double timer_get(TTimer *timer)
{
    LARGE_INTEGER t_now;
    QueryPerformanceCounter(&t_now);
   	double dt = (double)(t_now.QuadPart - timer->t_ref.QuadPart)/(double)timer->freq.QuadPart;
    return(dt);
} 


	


// read file with timeout
DWORD ReadFileTimeout(HANDLE file, LPVOID data, DWORD size, double timeout)
{    
    // get initial timestamp 
    TTimer timer;
    timer_init(&timer);
    
    DWORD read_total = 0;
    DWORD to_read = size;  
    do{
    
        // create ReadFile() async wait event
        HANDLE hEvent = CreateEvent( 
            NULL,    // default security attribute 
            true,    // manual-reset event 
            false,   // initial state 
            NULL);   // unnamed event object 
        if(!hEvent) 
        {
            return(0);
        }    
        OVERLAPPED overlap;
        overlap.Offset = 0;
        overlap.OffsetHigh = 0;
        overlap.hEvent = hEvent;
        
        // start async ReadFile()
        DWORD read;
        char *p = (char*)data;
        if(!ReadFile(file, (void*)&p[read_total], to_read, NULL, &overlap))
        {                
            DWORD err = GetLastError();
            if(err != ERROR_IO_PENDING)
            {
                if(DEBUG_PRN)
                    mexPrintf("read io not pending (err = %d)\n",err);
                CloseHandle(hEvent);
                return(0);
            }
        }
        
        // wait with timeout
        DWORD err = WaitForSingleObject(hEvent, (DWORD)(timeout*1000.0));
        if(err == WAIT_TIMEOUT)
        {
            // timeout
            //CancelIoEx(file, &overlap);
            if(DEBUG_PRN)
                mexPrintf("read timeout\n");
            CancelIo(file);
            CloseHandle(hEvent);
            return(0);
        }
            
        // get ReadFile() result
        DWORD fSuccess = GetOverlappedResult( 
            file, // handle to pipe 
            &overlap, // OVERLAPPED structure 
            &read,            // bytes transferred 
            false);            // do not wait
            
        read_total += read;
        to_read -= read;
                
        if(DEBUG_PRN)
            mexPrintf("read wait return code %d, read = %d, total read = %d\n",err,read,read_total);        
        
        // cleanup    
        CloseHandle(hEvent);
        
        if(timer_get(&timer) >= timeout)
        {
            // total timeout
            if(DEBUG_PRN)
                mexPrintf("read timeout\n");
            return(0);
        }
    
    }while(read_total < size);
    
    return(read_total);
}

// send ACK or NACK message
DWORD SendACK(HANDLE file, bool ack)
{
    char state = (ack)?'a':'n';
    DWORD written;
    WriteFile(file, &state, 1, &written, NULL);  
    char res;    
    ReadFileTimeout(file, &res, 1, 1.0);
    if(DEBUG_PRN)
        mexPrintf("ack response %d\n",(int)res);
    return(written);    
}


/* the gateway function */
DEFUN_DLD(golpi_pipe_receive, args, nargout, "Transfer variable to Octave using named pipe")
{
    // try get pipe name
    if(args.length() < 1)
        error("GOLPI pipe interface: At least name of the pipe must be passed.");                
    if(!args(0).is_string() || args(0).char_matrix_value().rows() != 1)
        error("GOLPI pipe interface: First argument must be pipe name string.");
    std::string pipe_name = args(0).char_matrix_value().row_as_string(0);
    
    // try get timeout parameter
    double timeout = 3.0;
    if(args.length() >= 2 && args(1).array_value().numel() == 1)
        timeout = args(1).array_value().elem(0);
    else
        error("GOLPI pipe interface: Second parameter must be double timeout value [s].");
        
    // try open pipe
    HANDLE hPipe;
    hPipe = CreateFileA(
		    pipe_name.c_str(), 
		    GENERIC_READ | GENERIC_WRITE, // access
        0, // sharing
        NULL, // security
        OPEN_EXISTING, // create mode
        FILE_FLAG_OVERLAPPED, // other
        NULL // template
        );    
    mxFree(pipe_name);      
  	if (hPipe == INVALID_HANDLE_VALUE)
        error("GOLPI pipe interface: Cannot access data pipe.");        
    
    // get data type
    DWORD var_type;
    DWORD read;
    read = ReadFileTimeout(hPipe, &var_type, sizeof(DWORD), timeout);
    if(!read)
    {
        SendACK(hPipe, false);
        CloseHandle(hPipe);
        error("GOLPI pipe interface: Timeout while transfering data type.");
    }
        
    // get dimensions
    DWORD m,n;
    read = ReadFileTimeout(hPipe, &m, sizeof(DWORD), timeout);
    if(!read)
    {
        SendACK(hPipe, false);
        CloseHandle(hPipe);
        error("GOLPI pipe interface: Timeout while transfering data size M.");
    }    
    read = ReadFileTimeout(hPipe, &n, sizeof(DWORD), timeout);
    if(!read)
    {
        SendACK(hPipe, false);
        CloseHandle(hPipe);
        error("GOLPI pipe interface: Timeout while transfering data size N.");
    }    
    bool is_empty = !m && !n;
    
    // get single element size
    DWORD element_size;
    switch(var_type)
    {
        case VTYPE_STRING: element_size = 1; break;
        case VTYPE_INT32: element_size = sizeof(DWORD); break;
        case VTYPE_UINT32: element_size = sizeof(DWORD); break;
        case VTYPE_DBL: element_size = sizeof(double); break;
        case VTYPE_CDBL: element_size = 2*sizeof(double); break;
        case VTYPE_SGL: element_size = sizeof(float); break;
        case VTYPE_CSGL: element_size = 2*sizeof(float); break;
        default: element_size = 0;
    }
    if(!element_size)
    {
        // unknown type
        SendACK(hPipe, false);
        CloseHandle(hPipe);
        error("GOLPI pipe interface: Unknown variable data type.");
    }
    DWORD data_size_bytes = m*n*element_size;
    
    // try read object data
    std:vector<char> data;
    if(!is_empty)
    {
        // allocate memory buffer
        data.reserve(data_size_bytes + 1);        
        // add string termination for strings
        data[data_size_bytes] = '\0';
        
        // try read data
        read = ReadFileTimeout(hPipe, data.data(), data_size_bytes, timeout);
        if(!read)
        {
            SendACK(hPipe, false);
            CloseHandle(hPipe);
            error("GOLPI pipe interface: Timeout while transfering data.");
        }        
    }    
    
    octave_value_list res;
    if(var_type == VTYPE_STRING)
    {
        // 1D string
        if(is_empty)
        {
            // empty string
            res = charMatrix("");
        }
        else if(m == 1 && n)
        {    
            // horizontal 1D string            
            res = charMatrix(data);
        }
        else
        {
            SendACK(hPipe, false);            
            CloseHandle(hPipe);
            error("GOLPI pipe interface: String can be only 1D horizontal.");
        }           
    }
    else if(var_type == VTYPE_DBL)
    {
        // double matrix
        
        // make output matrix
        mxArray *array = mxCreateNumericMatrix(m,n, mxDOUBLE_CLASS, mxREAL);
        if(!array)
        {
            SendACK(hPipe, false);
            mxFree(data);
            CloseHandle(hPipe);
            mexErrMsgIdAndTxt("GOLPI pipe interface", "Output matrix allocation failed.");
        }
            
        // fill data
        if(!is_empty)
        {
            memcpy((void*)mxGetPr(array), (void*)data, data_size_bytes);                
            mxFree(data);
        }
        
        // return
        plhs[0] = array;            
    }
    else if(var_type == VTYPE_CDBL)
    {
        // complex double matrix
        
        if(is_empty)
        {
            // empty matrix
            plhs[0] = mxCreateNumericMatrix(m,n, mxDOUBLE_CLASS, mxCOMPLEX);                          
        }
        else
        {
            // make temp real array
            mxArray *real_array = mxCreateNumericMatrix(m*n*2,1, mxDOUBLE_CLASS, mxREAL);
            if(!real_array)
            {
                SendACK(hPipe, false);
                mxFree(data);
                CloseHandle(hPipe);
                mexErrMsgIdAndTxt("GOLPI pipe interface", "Output matrix allocation failed.");
            }
            memcpy((void*)mxGetPr(real_array), (void*)data, data_size_bytes);                
            mxFree(data);
            
            // typecast to complex
            mxArray *cplx_array;
            mxArray *par_typecast[] = {real_array, mxCreateString("double complex")};
            mexCallMATLAB(1,&cplx_array, 2,par_typecast, "typecast");
            mxDestroyArray(real_array);
            
            // reshape to desired dims
            mxArray *size_array = mxCreateNumericMatrix(1,2, mxDOUBLE_CLASS, mxREAL);
            double *p_size_array = mxGetPr(size_array);
            p_size_array[0] = (double)m;
            p_size_array[1] = (double)n;
            mxArray *par_reshape[] = {cplx_array, size_array};
            mexCallMATLAB(1,&plhs[0], 2,par_reshape, "reshape");
            mxDestroyArray(cplx_array);
            mxDestroyArray(size_array);
        }
       
    }    
    else
    {
        if(data)
            mxFree(data);
        SendACK(hPipe, false);
        CloseHandle(hPipe);
        mexErrMsgIdAndTxt("GOLPI pipe interface", "Unknown variable data type.");
    }
    
    // send ACK
    SendACK(hPipe, true);
    
    // close pipe
    CloseHandle(hPipe);
    
    mexPrintf("GOLPImark\n");    
                
}


