/**********
Copyright (c) 2019, Xilinx, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********/
#include "xcl2.hpp"
#include <vector>

using std::vector;

static const int DATA_SIZE = 1024;
static const std::string error_message =
    "Error: Result mismatch:\n"
    "i = %d CPU result = %d Device result = %d\n";

// This example illustrates the very simple OpenCL example that performs
// an addition on two vectors
int main(int argc, char **argv) {
	int isize=0;
	int max_val,num_of_cols,num_of_rows;//maximum intensity value : 255
    
    string inputLine ="";
     if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <XCLBIN File>" << std::endl;
        return EXIT_FAILURE;
    }
	
	//read the image file
    ifstream image("targets.ppm",ios::binary);
    //detremine its size
 

    getline(image,inputLine);// read the first line : P5
    if(inputLine.compare("P2")!=0) cerr <<"Version error"<< endl;
    cout <<"Version : "<< inputLine << endl;

    getline(image,inputLine);// read the second line : comment
    //cout <<"Comment : "<< inputLine << endl;

   //read the third line : width and height
     image >> num_of_cols ;
    image >> num_of_rows;
    image >> max_val;
    isize= num_of_cols*num_of_rows ;
    
    
     
    std::string binaryFile = argv[1];
    //threshold_value = atoi(argv[2]);
    // compute the size of array in bytes
    size_t size_in_bytes = isize * sizeof(int);
    
    
    cl_int err;
    cl::CommandQueue q;
    cl::Kernel krnl_img_thresh;
    cl::Context context;

    // Creates a vector of DATA_SIZE elements with an initial value of 10 and 32
    unsigned int img_in[isize];
     unsigned int img_os[isize];
      unsigned int img_op[isize];
      for (unsigned int k=0; k<isize; k++){
		image>> img_in[k];	
	}

    // The get_xil_devices will return vector of Xilinx Devices
    auto devices = xcl::get_xil_devices();

    // read_binary_file() is a utility API which will load the binaryFile
    // and will return the pointer to file buffer.
    auto fileBuf = xcl::read_binary_file(binaryFile);
    cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};
    int valid_device = 0;
    for (unsigned int i = 0; i < devices.size(); i++) {
        auto device = devices[i];
        // Creating Context and Command Queue for selected Device
        OCL_CHECK(err, context = cl::Context({device}, NULL, NULL, NULL, &err));
        OCL_CHECK(err,
                  q = cl::CommandQueue(
                      context, {device}, CL_QUEUE_PROFILING_ENABLE, &err));

        std::cout << "Trying to program device[" << i
                  << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
        cl::Program program(context, {device}, bins, NULL, &err);
        if (err != CL_SUCCESS) {
            std::cout << "Failed to program device[" << i
                      << "] with xclbin file!\n";
        } else {
            std::cout << "Device[" << i << "]: program successful!\n";
            // This call will extract a kernel out of the program we loaded in the
            // previous line. A kernel is an OpenCL function that is executed on the
            // FPGA. This function is defined in the src/vetor_addition.cl file.
            OCL_CHECK(
                err, krnl_img_thresh = cl::Kernel(program, "threshold", &err));
            valid_device++;
            break; // we break because we found a valid device
        }
    }
    if (valid_device == 0) {
        std::cout << "Failed to program any device found, exit!\n";
        exit(EXIT_FAILURE);
    }

    // These commands will allocate memory on the FPGA. The cl::Buffer objects can
    // be used to reference the memory locations on the device. The cl::Buffer
    // object cannot be referenced directly and must be passed to other OpenCL
    // functions.
    OCL_CHECK(err,
              cl::Buffer buffer_a(context,
                                  CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                                  size_in_bytes,
                                  img_in,
                                  &err));
   
    OCL_CHECK(err,
              cl::Buffer buffer_result(context,
                                       CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY,
                                       size_in_bytes,
                                       img_op,
                                       &err));

    //set the kernel Arguments
    int narg = 0;
    OCL_CHECK(err, err = krnl_img_thresh.setArg(narg++, buffer_result));
    OCL_CHECK(err, err = krnl_img_thresh.setArg(narg++, buffer_a));
    
    OCL_CHECK(err, err = krnl_img_thresh.setArg(narg++, isize));

    // These commands will load the source_a and source_b vectors from the host
    // application and into the buffer_a and buffer_b cl::Buffer objects. The data
    // will be be transferred from system memory over PCIe to the FPGA on-board
    // DDR memory.
    OCL_CHECK(err,
              err = q.enqueueMigrateMemObjects({buffer_a},
                                               0 /* 0 means from host*/));

    //Launch the Kernel
    //This is equivalent to calling the enqueueNDRangeKernel function with a 
    //dimensionality of 1 and a global work size (NDRange) of 1.
    OCL_CHECK(err, err = q.enqueueTask(krnl_img_thresh));

    // The result of the previous kernel execution will need to be retrieved in
    // order to view the results. This call will write the data from the
    // buffer_result cl_mem object to the source_results vector
    OCL_CHECK(err,
              err = q.enqueueMigrateMemObjects({buffer_result},
                                               CL_MIGRATE_MEM_OBJECT_HOST));
    q.finish();

    int match = 0;
    printf("Result = \n");
    for (int i = 0; i < isize; i++) {
		int host_result;
		if(img_in[i]>150)
          img_op[i]=255;
         else img_op[i]=0;
        
    }
    for (int i = 0; i < isize; i++) {
    if (img_os[i] != img_op[i]) {
            printf(error_message.c_str(), i, host_result, source_results[i]);
            match = 1;
            break;
        } 
 }
    std::cout << "TEST " << (match ? "FAILED" : "PASSED") << std::endl;
    return (match ? EXIT_FAILURE : EXIT_SUCCESS);
}
