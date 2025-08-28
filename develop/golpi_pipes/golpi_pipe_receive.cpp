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
#include <octave/oct.h>
#include <windows.h>

// variable type IDs
#define VTYPE_STRING 0 /* simple string */
#define VTYPE_INT32 1 /* 32bit signed integer */
#define VTYPE_UINT32 2 /* 32bit unsigned integer */
#define VTYPE_DBL 3 /* 64bit float */
#define VTYPE_CDBL 4 /* 64bit complex float (re,im,re,im, ...) */
#define VTYPE_SGL 5 /* 32bit float */
#define VTYPE_CSGL 6 /* 32bit complex float (re,im,re,im, ...) */

// enable some debug prints
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
DWORD ReadFileTimeout(HANDLE file, LPVOID data, DWORD size, DWORD *read_bytes, double timeout)
{    
    // default no data read
    if(read_bytes)
        *read_bytes = 0;
             
    // empty data?
    if(!size)
        return(0);
    
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
            return(1);    
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
                    octave_stdout << "read io not pending (err = " << err << ")\n";
                CloseHandle(hEvent);
                return(1);
            }
        }
        
        // wait with timeout
        DWORD err = WaitForSingleObject(hEvent, (DWORD)(timeout*1000.0));
        if(err == WAIT_TIMEOUT)
        {
            // timeout
            //CancelIoEx(file, &overlap);
            if(DEBUG_PRN)
                octave_stdout << "read timeout\n";
            CancelIo(file);
            CloseHandle(hEvent);
            return(1);
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
            octave_stdout << "read wait return code " << err << ", read = " << read << ", total read = " << read_total << "\n";        
        
        // cleanup    
        CloseHandle(hEvent);
        
        if(timer_get(&timer) >= timeout)
        {
            // total timeout
            if(DEBUG_PRN)
                octave_stdout << "read timeout\n";
            return(1);
        }
    
    }while(read_total < size);
    
    // return total read bytes
    if(read_bytes)
        *read_bytes = read_total;
    
    return(0);
}

// send ACK or NACK message
DWORD SendACK(HANDLE file, bool ack)
{
    char state = (ack)?'a':'n';
    DWORD written;
    WriteFile(file, &state, 1, &written, NULL);  
    char res;    
    ReadFileTimeout(file, &res, 1, NULL, 1.0);
    if(DEBUG_PRN)
        octave_stdout << "ack response " << (int)res << "\n";
    return(written);    
}


/* the gateway function */
DEFUN_DLD(golpi_pipe_receive, args, nargout, "Transfer variable to Octave using named pipe")
{
    // outputs
    if(nargout != 1)
        error("GOLPI pipe interface: One output argument expected - destination variable.");
    
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
    else if(args.length() >= 2)
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
  	if (hPipe == INVALID_HANDLE_VALUE)
        error("GOLPI pipe interface: Cannot access data pipe.");        
    
    // get data type
    DWORD var_type;
    DWORD read;    
    if(ReadFileTimeout(hPipe, &var_type, sizeof(DWORD), &read, timeout))
    {
        SendACK(hPipe, false);
        CloseHandle(hPipe);
        error("GOLPI pipe interface: Timeout while transfering data type.");
    }
        
    // get dimensions
    DWORD m,n;
    if(ReadFileTimeout(hPipe, &m, sizeof(DWORD), &read, timeout))
    {
        SendACK(hPipe, false);
        CloseHandle(hPipe);
        error("GOLPI pipe interface: Timeout while transfering data size M.");
    }    
    if(ReadFileTimeout(hPipe, &n, sizeof(DWORD), &read, timeout))
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
    
    
    octave_value_list res;
    if(var_type == VTYPE_STRING)
    {
        // 1D string
        if(is_empty)
        {
            // empty string
            res(0) = charMatrix("");
        }
        else if(m == 1 && n)
        {    
            // horizontal 1D string
            
            // try read data
            charMatrix str = charMatrix(m,n);
            if(ReadFileTimeout(hPipe, (void*)str.fortran_vec(), data_size_bytes, &read, timeout))
            {
                SendACK(hPipe, false);
                CloseHandle(hPipe);
                error("GOLPI pipe interface: Timeout while transfering data.");
            }
            res(0) = str;
        }
        else
        {
            SendACK(hPipe, false);            
            CloseHandle(hPipe);
            error("GOLPI pipe interface: String can be only 1D horizontal.");
        }           
    }
    else if(var_type == VTYPE_INT32)
    {
        // int32 matrix
        
        // try read data
        int32NDArray array = int32NDArray({m,n});
        if(ReadFileTimeout(hPipe, (void*)array.fortran_vec(), data_size_bytes, &read, timeout))
        {
            SendACK(hPipe, false);
            CloseHandle(hPipe);
            error("GOLPI pipe interface: Timeout while transfering data.");
        }
        res(0) = array;
    }
    else if(var_type == VTYPE_UINT32)
    {
        // uint32 matrix
        
        // try read data
        uint32NDArray array = uint32NDArray({m,n});
        if(ReadFileTimeout(hPipe, (void*)array.fortran_vec(), data_size_bytes, &read, timeout))
        {
            SendACK(hPipe, false);
            CloseHandle(hPipe);
            error("GOLPI pipe interface: Timeout while transfering data.");
        }
        res(0) = array;
    }
    else if(var_type == VTYPE_DBL)
    {
        // double matrix
        
        // try read data
        Matrix array = Matrix(m,n);
        if(ReadFileTimeout(hPipe, (void*)array.fortran_vec(), data_size_bytes, &read, timeout))
        {
            SendACK(hPipe, false);
            CloseHandle(hPipe);
            error("GOLPI pipe interface: Timeout while transfering data.");
        }
        res(0) = array;
    }
    else if(var_type == VTYPE_SGL)
    {
        // single matrix
        
        // try read data
        FloatMatrix array = FloatMatrix(m,n);
        if(ReadFileTimeout(hPipe, (void*)array.fortran_vec(), data_size_bytes, &read, timeout))
        {
            SendACK(hPipe, false);
            CloseHandle(hPipe);
            error("GOLPI pipe interface: Timeout while transfering data.");
        }
        res(0) = array;
    }
    else if(var_type == VTYPE_CDBL)
    {
        // complex double matrix
        
        // try read data
        ComplexMatrix array = ComplexMatrix(m,n);
        if(ReadFileTimeout(hPipe, (void*)array.fortran_vec(), data_size_bytes, &read, timeout))
        {
            SendACK(hPipe, false);
            CloseHandle(hPipe);
            error("GOLPI pipe interface: Timeout while transfering data.");
        }
        res(0) = array;
    }
    else if(var_type == VTYPE_CSGL)
    {
        // complex single matrix
        
        // try read data
        FloatComplexMatrix array = FloatComplexMatrix(m,n);
        if(ReadFileTimeout(hPipe, (void*)array.fortran_vec(), data_size_bytes, &read, timeout))
        {
            SendACK(hPipe, false);
            CloseHandle(hPipe);
            error("GOLPI pipe interface: Timeout while transfering data.");
        }
        res(0) = array;
    }        
    else
    {
        SendACK(hPipe, false);
        CloseHandle(hPipe);
        error("GOLPI pipe interface: Unknown variable data type.");
    }
    
    // send ACK
    SendACK(hPipe, true);
    
    // close pipe
    CloseHandle(hPipe);
    
    // console sync mark
    octave_stdout << "GOLPImark\n";
    
    // return stuff
    return res;    
}


