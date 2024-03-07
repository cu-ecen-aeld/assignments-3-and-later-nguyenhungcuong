#include <stdio.h>
#include <syslog.h>

int main(int argc, char* argv[])
{
    // check parameters
    if (argc < 2) {
        syslog(LOG_ERR, "Usage: need 2 parameters!");
        return 1;
    }

    // get parameters
    const char * writefile = argv[1];
    const char * writestr = argv[2];

    // set up log
    openlog(NULL, 0, LOG_USER);

    // open file
    FILE *fp = fopen(writefile, "w");
    if(fp != NULL)
    {
        // write string to file
        int byte_write = fputs(writestr, fp);
        if (byte_write == EOF)
        {
            syslog(LOG_ERR, "Error writing to file!\n");
            fclose(fp); // Close the file even on error 
            return 1; 
        }

        fclose(fp);
        syslog(LOG_DEBUG, "Writing %s to %s\n", writestr, writefile);
    }
    else
    {
        syslog(LOG_ERR, "Error open file!\n");
        return 1;
    }

    return 0;
}
