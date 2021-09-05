#include <cstdlib>
#include <math.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <utility>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/random/random_device.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include "server.h"



using boost::asio::ip::tcp;


/*-----------------------------------------------------------------------------------------------------------------*/
/* Global variables */
std::list<uint32_t> _clients; // will hold the ID's of all the clients that were connected


/*-----------------------------------------------------------------------------------------------------------------*/
/* function defenitions */
void printBuffer(uint8_t* buf, uint32_t length);
bool findId(std::list<uint32_t > list, uint32_t id);
void clear_buffer(uint8_t* buf, uint32_t length);
void server(boost::asio::io_context& io_context, unsigned short port);
void session(tcp::socket sock);
bool mkdir(uint32_t userID);
std::string generateRandomAlphaNum(const int len);
std::string createDirListFile(std::string path, std::vector<std::string> dirList);
std::vector<std::string> getDirList(std::string path);
std::vector<uint8_t> buildResponse(Response* response, uint16_t retCode=0, uint16_t nameLen=0, std::string filename="", uint32_t size=0);
uint16_t validateRequestValues(Request* request);
uint16_t processRequest(tcp::socket& sock, Request* request, Response* response);
uint16_t backupFile(tcp::socket& sock, Request* request);
uint16_t retrieveFileFromBackup(tcp::socket& sock, Request* request, Response* response);
uint16_t eraseFile(std::string path);
uint16_t sendDirListFile(tcp::socket& sock, Request* request, Response* response, std::string fileName, std::vector<std::string> dirList);
/*-----------------------------------------------------------------------------------------------------------------*/







/*
* Receive and process client's request
*/
void session(tcp::socket sock)
{
    boost::system::error_code error;
    
    bool RequestRead = false;
    uint8_t data[MAX_LENGTH] = {0};
    uint32_t offset = 0;
    size_t length;
    uint16_t status = 0;


    // create a sturct to hold client's Request
    Request* request = new Request;
    // create a sturct to hold client's payload
    Response* response = new Response;



    try
    {
        /* first of all read the Request + size, once */
        length = sock.read_some(boost::asio::buffer(data), error);


        /* insert the appropriate bytes to their corresponding field in the Request
            this is done according to the defined Request format */

        /* insert the client's user id */
        request->userid = data[3];
        request->userid = (request->userid << 8) + data[2];
        request->userid = (request->userid << 8) + data[1];
        request->userid = (request->userid << 8) + data[0];

        /* insert version */
        request->version = data[4];

        /* insert operation */
        request->op = data[5];

        /* insert filename length */
        request->nameLen = data[7];
        request->nameLen = (request->nameLen << 8) + data[6];

        /* copy the filename. only copy nameLen amount of bytes */
        for (offset = 8; offset < (8 + request->nameLen); offset++)
            request->filename += data[offset];

        
        /* read the file size into the size field in payload */
        request->size = data[offset + 3];
        request->size = (request->size << 8) + data[offset + 2];
        request->size = (request->size << 8) + data[offset + 1];
        request->size = (request->size << 8) + data[offset + 0];


        /* if the client is new, than add him to the clients list */
        if (!findId(_clients, request->userid))
            _clients.push_back(request->userid);

        /* check whether the values of all the fields in the Request are according to protocol */
        if (!validateRequestValues(request)) {
            throw boost::system::system_error(error);
        }
        std::cout << "User ID: " << std::to_string(request->userid) << std::endl;
        

        
        /* address the request and act appropriately */
        status = processRequest(sock, request, response);



        if (error)
            throw boost::system::system_error(error); // Some other error.
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception in thread, session: " << e.what() << "\n";
    }
    std::cout << "Session ended with status: " << std::to_string(status) << std::endl;


    // make sure to delete the allocated objects that handled the current client
cleanup:
    std::cout << "Deleting temporarly allocated resources for request" << std::endl;
    delete(request);
    delete(response);
}



/*
* insures that some of the recieved values of the Request are valid according to protocol
*/
uint16_t validateRequestValues(Request * request)
{
    // if version is incorrect
    if (request->version != VERSION_CLIENT)
        return false;
    
    // if operation is illegal. there is additional check later in processRequest
    if (request->op != BACKUP_FILE &&
        request->op != GET_FILE &&
        request->op != ERASE_FILE &&
        request->op != GET_BACKUP_LIST)
        return false;

    // if the filename contains '\0's instead of characters 
    if (request->nameLen != request->filename.size())
        return false;

    // all the above checks have passed
    return true;
}


/*
* address the operation in the received request, and perform the appropriate
* sequence of tasks.
*/
uint16_t processRequest(tcp::socket& sock, Request * request, Response * response)
{
    uint16_t retCode = 0;
    std::string path = SERVER_BACKUP_PARENT_DIR;
    std::string dirListfileName = "";
    std::string seq = "";
    std::vector<uint8_t> resArr;
    std::vector<std::string> dirList;

    try
    {
        switch (request->op)
        {
            //--------------------------------------------------------------------------------------------
            //--------------------------------------------------------------------------------------------
        case BACKUP_FILE:
            std::cout << "Saving file for backup: " << request->filename << std::endl;

            // make sure that the given file size is not larger than uint32
            if (request->size >= pow(2, 32)){
                std::cout << "File size to large (larger than 2^32 bytes)" << std::endl;
                resArr = buildResponse(response, GENERAL_ERROR);
                boost::asio::write(sock, boost::asio::buffer(resArr));
                return GENERAL_ERROR;
            }

            // check for existance of client's directory. create it if needed
            if (!mkdir(request->userid)) {
                std::cout << "Error opening client's directory" << std::endl;
                resArr = buildResponse(response, GENERAL_ERROR);
                boost::asio::write(sock, boost::asio::buffer(resArr));
                return GENERAL_ERROR;
            }

            retCode = backupFile(sock, request);
            
            resArr = buildResponse(response, retCode, request->nameLen, response->filename);
            boost::asio::write(sock, boost::asio::buffer(resArr));
            break;


        
            
            //--------------------------------------------------------------------------------------------
            //--------------------------------------------------------------------------------------------
        case GET_FILE:
            std::cout << "Retreaving file from backup: " << request->filename << std::endl;

            path += std::to_string(request->userid);

            // check if client's directory actually exists
            if (!boost::filesystem::exists(path) && !boost::filesystem::is_directory(path)) {
                std::cout << "Error opening client's directory" << std::endl;
                resArr = buildResponse(response, NO_FILES_FOR_CLIENT);
                boost::asio::write(sock, boost::asio::buffer(resArr));
                return NO_FILES_FOR_CLIENT;
            }

            path += ("\\" + request->filename);

            // check if the required file exists
            if (!boost::filesystem::exists(path)) {
                std::cout << "Client's file does not exist" << std::endl;
                resArr = buildResponse(response, FILE_NOT_FOUND, request->nameLen, response->filename);
                boost::asio::write(sock, boost::asio::buffer(resArr));
                return FILE_NOT_FOUND;
            }
            
            // check if the file is not empty
            if (boost::filesystem::file_size(path) == 0) {
                std::cout << "Client's file size is 0" << std::endl;
                resArr = buildResponse(response, FILE_NOT_FOUND, request->nameLen, response->filename);
                boost::asio::write(sock, boost::asio::buffer(resArr));
                return FILE_NOT_FOUND;
            }

            retCode = retrieveFileFromBackup(sock, request, response);
            
            if (retCode != GET_FILE_SUCCESS) {
                resArr = buildResponse(response, retCode, request->nameLen, response->filename);
                boost::asio::write(sock, boost::asio::buffer(resArr));
            }
            break;
        


            //--------------------------------------------------------------------------------------------
            //--------------------------------------------------------------------------------------------
        case ERASE_FILE:
            std::cout << "Erasing file from backup: " << request->filename << std::endl;
            
            path += std::to_string(request->userid);

            // check if client's directory actually exists
            if (!boost::filesystem::exists(path) && !boost::filesystem::is_directory(path)) {
                std::cout << "Error opening client's directory" << std::endl;
                resArr = buildResponse(response, NO_FILES_FOR_CLIENT);
                boost::asio::write(sock, boost::asio::buffer(resArr));
                return NO_FILES_FOR_CLIENT;
            }

            path += ("\\" + request->filename);

            // check if the required file exists
            if (!boost::filesystem::exists(path)) {
                std::cout << "Client's file does not exist" << std::endl;
                resArr = buildResponse(response, FILE_NOT_FOUND, request->nameLen, response->filename);
                boost::asio::write(sock, boost::asio::buffer(resArr));
                return FILE_NOT_FOUND;
            }

            retCode = eraseFile(path);
            resArr = buildResponse(response, retCode, request->nameLen, response->filename);
            boost::asio::write(sock, boost::asio::buffer(resArr));
            break;

        
            //--------------------------------------------------------------------------------------------
            //--------------------------------------------------------------------------------------------
        case GET_BACKUP_LIST:
            std::cout << "Returning files list for client: " << request->userid << std::endl;

            path += std::to_string(request->userid);

            // check if client's directory actually exists
            if (!boost::filesystem::exists(path) && !boost::filesystem::is_directory(path)) {
                std::cout << "Error opening client's directory" << std::endl;
                resArr = buildResponse(response, NO_FILES_FOR_CLIENT);
                boost::asio::write(sock, boost::asio::buffer(resArr));
                return NO_FILES_FOR_CLIENT;
            }

            // get the list of files in client's directory
            dirList = getDirList(path);

            if (dirList.empty()) {
                std::cout << "Client's directory is empty" << std::endl;
                resArr = buildResponse(response, NO_FILES_FOR_CLIENT);
                boost::asio::write(sock, boost::asio::buffer(resArr));
                return NO_FILES_FOR_CLIENT;
            }
            

            seq = generateRandomAlphaNum(32) + ".txt";
            
            
            // send the dir list
            retCode = sendDirListFile(sock, request, response, seq, dirList);

            if (retCode != GET_BACKUP_LIST_SUCCESS) {
                std::cout << "Error occured while sending dir list file" << std::endl;
                resArr = buildResponse(response, GENERAL_ERROR);
                boost::asio::write(sock, boost::asio::buffer(resArr));
            }

            break;

        

        default:
            std::string err = std::to_string(request->op);
            throw err;
        }
    }
    catch (const char* err)
    {
        std::cerr << "Exeption in thread, processRequest: Requested operation " << err << "not available\n";
        retCode = GENERAL_ERROR;
    }
    
    return retCode;
}



/*
* perfrom a backup operation. receive a file from the client and save it
* in the client's directory
*/
uint16_t backupFile(tcp::socket& sock, Request * request)
{
    boost::system::error_code error;
    uint32_t size = 0;
    uint32_t byteCount = 0;
    uint8_t chunk[MAX_LENGTH] = { 0 };
    std::string path = SERVER_BACKUP_PARENT_DIR;
    
    
    
    std::cout << "Attempting to create file at path: " << std::to_string(request->userid) + "\\" << request->filename << std::endl;
    path += (std::to_string(request->userid) + "\\" + request->filename);

    
    // attempt to create the file to backup (append mode)
    std::ofstream file;
    try
    {
        file.open(path, std::ios::out | std::ios::binary);
        if (!file)
            throw std::exception("File not open");
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in thread, readFileAndBackup: " << e.what() << "\n";
        return GENERAL_ERROR;
    }

    // attempt to receive the incoming packets and write their data to the created file
    try
    {
        while (byteCount < request->size)
        {    
            size = sock.read_some(boost::asio::buffer(chunk), error);
            byteCount += size;


            if ((error == boost::asio::error::eof) || (size == 0)) {
                std::cout << "EOF" << "\n";
                break;
            }
            else if (error)
                throw boost::system::system_error(error); // Some other error.
           

            // write/append the chunk to the created file
            file.write((char*) &chunk, size);

            // clear the data buffer before the next read
            clear_buffer(chunk, MAX_LENGTH);
        }

        if (byteCount != request->size) {
            std::cout << "Mismatch. Number of received file bytes != file size" << std::endl;
            throw  boost::system::system_error(error);
        }
        
        std::cout << "Read " << byteCount << " bytes" << std::endl;

    }
    catch (std::exception& e)
    {
        std::cerr << "Exception in thread, readFileAndBackup: " << e.what() << "\n";
        file.close();
        return GENERAL_ERROR;
    }
    
    file.close();
    return BACKUP_FILE_SUCCESS;
}


/*
* Send back the file that the client has specified.
*/
uint16_t retrieveFileFromBackup(tcp::socket& sock, Request* request, Response* response)
{
    boost::system::error_code error;
    std::string path = SERVER_BACKUP_PARENT_DIR;
    uint32_t size = 0;
    uint32_t byteCount = 0;
    uint8_t chunk[MAX_LENGTH] = { 0 };
    std::vector<uint8_t> resArr;
    

    std::cout << "Retrieving file: " << request->filename << std::endl;
    path += (std::to_string(request->userid) + "\\" + request->filename);


    // attempt to open the file
    std::ifstream file;
    try
    {
        file.open(path, std::ios::out | std::ios::binary);
        if (!file)
            throw std::exception("File not open");
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in thread, readFileAndBackup: " << e.what() << "\n";
        return FILE_NOT_FOUND;
    }

    
    
    /* first, send the header of the response. then send the payload of the file */
    resArr = buildResponse(response, GET_FILE_SUCCESS, 
                            request->nameLen, 
                            request->filename, 
                            boost::filesystem::file_size(path));
    
    try
    {
        // send the response header
        boost::asio::write(sock, boost::asio::buffer(resArr));

        // send the file payload
        while (byteCount < boost::filesystem::file_size(path))
        {
            if (file.eof()) // check if file pointer reached the end of the file
                break;
            
            file.read((char*)&chunk, MAX_LENGTH);  // try to read MAX_LANGTH amount of bytes
            size = file.gcount(); // get amount of bytes that were read successfuly
            byteCount += size;

            // send the chunk to client
            boost::asio::write(sock, boost::asio::buffer(chunk));
            
            if (error)
                throw boost::system::system_error(error); // Some other error.

            // clear the data buffer before the next read
            clear_buffer(chunk, MAX_LENGTH);
        }

        std::cout << "Sent " << byteCount << " bytes" << std::endl;

    }
    catch (std::exception& e)
    {
        std::cerr << "Exception in thread, readFileAndBackup: " << e.what() << "\n";
        file.close();
        return GENERAL_ERROR;
    }

    file.close();
    return GET_FILE_SUCCESS;
}


/*
* Erase client's specified file from his dirctory in the server
*/
uint16_t eraseFile(std::string path)
{
    try
    {
        boost::filesystem::remove(path);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in thread, eraseFile: " << e.what() << "\n";
        return GENERAL_ERROR;
    }
    return ERASE_FILE_SUCCESS;
}


/*
* Retruns a vector where each element is a string representing a filename in the client's directory.
*/
std::vector<std::string> getDirList(std::string path)
{
    std::vector<std::string> listOfFiles;

    try
    {
        std::cout << std::endl;


        
        for (const auto& entry : boost::filesystem::directory_iterator(path)) {
            std::cout << entry.path().filename().string() << std::endl;
            listOfFiles.push_back(entry.path().filename().string());
        }
        
        
        std::cout << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in thread, eraseFile: " << e.what() << "\n";
        
    }
    return listOfFiles;
}


/*
* Send back a list with the client's current files in his directory.
*/
uint16_t sendDirListFile(tcp::socket& sock, Request* request, Response* response, std::string fileName, std::vector<std::string> dirList)
{
    boost::system::error_code error;
    std::vector<uint8_t> resArr;
    uint32_t byteCount = 0;


    std::cout << "Sending dir list file:             " << fileName << std::endl;    

    /* first, send the header of the response. then send the payload of the file */
    resArr = buildResponse(response,
                            GET_BACKUP_LIST_SUCCESS,
                            36, // 32 + .txt
                            fileName,                                                
                            dirList.size());
    try
    {
        // send the response header
        boost::asio::write(sock, boost::asio::buffer(resArr));

        
        // send vector elements
        for (std::vector<std::string>::iterator line = dirList.begin(); line != dirList.end(); ++line) {
            
            // add a 'new line' seperator between lines inside the payload, so that
            // the client will parse it and print out seperate lines
            boost::asio::write(sock, boost::asio::buffer(*line + '\n'));
        
            byteCount += (*line).size();
            if (error)
                throw boost::system::system_error(error); // Some other error.
        }

        std::cout << "Sent " << byteCount << " bytes" << std::endl;
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception in thread, readFileAndBackup: " << e.what() << "\n";
        return GENERAL_ERROR;
    }
    return GET_BACKUP_LIST_SUCCESS;
}



std::string generateRandomAlphaNum(const int len)
{
    std::string s = "";
    std::string chars =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    s.reserve(len);
    boost::random::random_device rng;
    boost::random::uniform_int_distribution<> index_dist(0, chars.size() - 1);
    
    for (int i = 0; i < len; i++) {
        s += chars[index_dist(rng)];
    }
    return s;
}


/*
* creating a text file that saves the dir list.
* (not used in the project due to some clarifications in the mmn14 forum).
*/
std::string createDirListFile(std::string path, std::vector<std::string> dirList)
{
    // create 32 random alpha-numeric character sequence
    std::string seq = generateRandomAlphaNum(32) + ".txt";
    
    // attempt to open the file
    std::ofstream file;
    try
    {
        file.open(path + seq);
        if (!file)
            throw std::exception("File not open");
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in thread, createDirListFile: " << e.what() << "\n";
        return "";
    }



    std::cout << "Creating list of files for client: " << seq << std::endl;
    try
    {
        for (std::size_t i = 0; i < dirList.size(); i++) {
            file << dirList[i] << std::endl;
        }       
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in thread, createDirListFile: " << e.what() << "\n";
        return "";
    }

    file.close();
    return seq;
}


/*
* modifies the server's response according to the given parameters
* and reflects that in the contents of a given response array: resArr.
*/
std::vector<uint8_t> buildResponse( Response* response,
                    uint16_t retCode,
                    uint16_t nameLen,
                    std::string filename,
                    uint32_t size)
{
    /* fill the response buffer with the appropriate values
       the size of the buffer depends on:
       version    1 byte
       status     2 bytes
       name_len   2 bytes
       filename   variable number of bytes
       size       4 bytes
       payload    variable number of bytes
    */
    
    // modify response struct
    response->version = VERSION_SERVER;
    response->status = retCode;
    response->nameLen = nameLen;
    response->filename = filename;
    response->size = size;

    std::vector<uint8_t> resArr;
    resArr.push_back(VERSION_SERVER);
    resArr.push_back((uint8_t)retCode);
    resArr.push_back((uint8_t)(retCode >> 8));
    resArr.push_back((uint8_t)nameLen);
    resArr.push_back((uint8_t)(nameLen >> 8));
    
    if (!filename.empty()) {
        for (int i = 0; i < nameLen; i++)
            resArr.push_back(filename[i]);
    }
    if (size){
        resArr.push_back((uint8_t)(size));
        resArr.push_back((uint8_t)(size >> 8));
        resArr.push_back((uint8_t)(size >> 16));
        resArr.push_back((uint8_t)(size >> 24));
    }
    return resArr;
}





/*
* create a backup directory for the specified user id.
* this directory will be created inside the SERVER_BACKUP_PARENT_DIR const path.
* also checks if the directory already exists.
* return true upon success, else return false.
*/
bool mkdir(uint32_t userID)
{
    std::string path = SERVER_BACKUP_PARENT_DIR;
    path += std::to_string(userID);
    
    std::cout << "path: " << path << std::endl;
    

    // if directory already exists, exit
    if (boost::filesystem::exists(path))
        return true;
    
    try
    {
        boost::filesystem::create_directory(path);
        return true;
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception in thread, mkdir: " << e.what() << "\n";
        return false;
    }
}



/*
* Checks if a given client id exists in the given list
*/
bool findId(std::list<uint32_t > list, uint32_t id)
{
    for (std::list<uint32_t>::iterator it = list.begin(); it != list.end(); ++it) {
        if (*it == id)
            return true;
    }
    return false;
}


/*
* Infinite loop that listens to incoming client connections.
* once a client is connected, a new thread is created.
* this thread activates the session function which handles the client's request.
*/
void server(boost::asio::io_context& io_context, unsigned short port)
{
    tcp::acceptor a(io_context, tcp::endpoint(tcp::v4(), port));
    for (;;)
    {
        std::cout << "\n\n" << "Waiting to accept client connection" << "\n";


        std::thread(session, a.accept()).detach();


        std::cout << "Accepted " << "\n\n";
    }
}


void printBuffer(uint8_t * buf, uint32_t length) {
    for (uint32_t i = 0; i < length; i++)
    {
        //std::cout << std::hex << buf[i] << ":";
        printf("%02x:", buf[i]);
    }
    std::cout << std::endl;
    std::cout << std::endl;
}


void clear_buffer(uint8_t * buf, uint32_t length) {
    for (uint32_t i = 0; i < length; i++)
        buf[i] = 0;
}


int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2)
        {
            std::cerr << "Usage: blocking_tcp_echo_server <port>\n";
            return 1;
        }
        std::cout << "Starting Backup Server" << std::endl;


        boost::asio::io_context io_context;
        server(io_context, std::atoi(argv[1]));
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception in main: " << e.what() << "\n";
    }

    return 0;
}
