/*
 * Copyright (C) 2012-2014 Michael Richardson <mcr@sandelman.ca>
 *
 * SEE FILE COPYING in root of source.
 */

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <linux/if.h>
#include <sys/time.h>
#include <getopt.h>

#include "pathnames.h"
#include "oswlibs.h"
#include "rpl.h"

#include "hexdump.c"
}

#include "iface.h"
#include "fakeiface.h"

/* open a raw IPv6 socket, and
   - send a RPL Destination Advertisement Object for a specific host.
   - send data from file (in hex)  (-d)
*/

static void usage(void)
{
        fprintf(stderr, "Usage: senddao [--sequence #] [--instance #] [--ack-request] \n");
        fprintf(stderr, "               [--dagid hexstring]\n");
        fprintf(stderr, "               [--daoack seqnum]\n");
        fprintf(stderr, "               [--dest target] [--prefix prefix]\n");
        fprintf(stderr, "               [--target prefix...]\n");
        fprintf(stderr, "               [-d datafile] [--outpcap file --fake] [--fake] [--iface net]\n");

        exit(2);
}

unsigned int read_hex_values(FILE *in, unsigned char *buffer)
{
    int count = 0;
    unsigned int data;
    int c;

    while((c = fgetc(in)) != EOF) {
        if(c == '#') {
            /* skip comment */
            while((c = fgetc(in)) != EOF &&
                  c != '\n');
            if(c==EOF) return count;
            continue;
        }
        ungetc(c, in);
        while(fscanf(in, "%2x", &data) > 0) {
            buffer[count++]=data;
        }
    }
    return count;
}

bool check_dag(instanceID_t id, dag_network *dag)
{
    if(dag==NULL || id == 0) {
	fprintf(stderr, "--dagid and --instanceID must preceed DODAG parameters\n");
	usage();
    }
    return true;
}

int main(int argc, char *argv[])
{
    int c;
    const char *datafilename;
    instanceID_t instanceID = 0;
    FILE *datafile;
    char *outfile     = NULL;
    char *prefixvalue = NULL;
    char *targetaddr  = NULL;
    unsigned char icmp_body[2048];
    unsigned int  icmp_len = 0;
    unsigned int verbose=0;
    unsigned int fakesend=0;
    struct option longoptions[]={
        {"debug",    0, NULL, 'v'},
        {"verbose",  0, NULL, 'v'},
        {"fake",     0, NULL, 'T'},
        {"testing",  0, NULL, 'T'},
        {"prefix",   1, NULL, 'p'},
        {"target",   1, NULL, 'a'},
        {"sequence", 1, NULL, 'S'},
        {"instance",   1, NULL, 'I'},
        {"instanceid", 1, NULL, 'I'},
        {"dagid",    1, NULL, 'G'},
        {"daoack",   1, NULL, 'K'},
        {"datafile", 1, NULL, 'd'},
        {"dest",     1, NULL, 't'},
        {"myid",     1, NULL, 'M'},
        {"ack-request",0,NULL,'A'},
        {"iface",    1, NULL, 'i'},
        {"outpcap",  1, NULL, 'O'},
        {0,0,0,0},
    };

    class rpl_debug *deb = new rpl_debug(verbose, stderr);
    class network_interface *iface = NULL;
    class dag_network       *dn = NULL;
    class pcap_network_interface *piface = NULL;
    struct in6_addr target, *dest = NULL;
    struct in6_addr myid;
    prefix_node *dag_me = NULL;
    rpl_node *destnode = NULL;
    bool initted = false;
    bool ackreq  = false;
    bool daoack  = false;
    memset(icmp_body, 0, sizeof(icmp_body));

    while((c=getopt_long(argc, argv, "AD:G:I:KO:R:S:Ta:d:i:h?p:t:v", longoptions, NULL))!=EOF){
        switch(c) {
        case 'A':
            ackreq=true;  /* XXX todo */
            break;

        case 'd':
            datafilename=optarg;
            if(datafilename[0]=='-' && datafilename[1]=='\0') {
                datafile = stdin;
                datafilename="<stdin>";
            } else {
                datafile = fopen(datafilename, "r");
            }
            if(!datafile) {
                perror(datafilename);
                exit(1);
            }
            icmp_len = read_hex_values(datafile, icmp_body);
            break;

        case 'i':
            if(!initted) {
                if(fakesend) {
                    fprintf(stderr, "Using faked interfaces\n");
                    pcap_network_interface::scan_devices(deb, false);
                } else {
                    network_interface::scan_devices(deb, false);
                }
                initted = true;
            }
            if(outfile == NULL) {
                    fprintf(stderr, "Must specify pcap outfile (-O)\n");
                    usage();
                    exit(2);
            }

            iface = network_interface::find_by_name(optarg);
            if(iface && fakesend) {
                if(iface->faked()) {
                    piface = (pcap_network_interface *)iface;
                    piface->set_pcap_out(outfile, DLT_EN10MB);
                } else {
                    fprintf(stderr, "interface %s is not faked\n", optarg);
                    exit(1);
                }
            }
            break;

        case 'T':
            if(initted) {
                fprintf(stderr, "--fake MUST be first argument\n");
                exit(16);
            }
            fakesend=1;
            break;

        case 'O':
            outfile=optarg;
            break;

        case 'G':
            if(!iface) {
                fprintf(stderr, "--dagid must follow --interface argument\n");
                usage();
                exit(17);
            }
            dn = iface->find_or_make_dag_by_instanceid(instanceID, optarg);
            break;

        case 'K':
            daoack = atoi(optarg);
            break;

        case 'R':
	    check_dag(instanceID, dn);
            dn->set_dagrank(atoi(optarg));
            break;

        case 'S':
	    check_dag(instanceID, dn);
            dn->set_sequence(atoi(optarg));
            break;

        case 'I':
            instanceID = atoi(optarg);
            break;

        case 'a':
            {
                char *targetvalue = optarg;
                ip_subnet targetaddr;
                err_t e = ttosubnet(targetvalue, strlen(targetvalue),
                                    AF_INET6, &targetaddr);
                check_dag(instanceID, dn);
                if(!dest) {
                    fprintf(stderr, "destination must be set before target addresses\n");
                    usage();
                }
                if(dag_me == NULL) {
                    fprintf(stderr, "myid must be set before target addresses\n");
                    usage();
                }
                dn->add_childnode(destnode, iface, targetaddr);
            }
            break;

        case 'p':
            prefixvalue = optarg;
            {
                ip_subnet prefix;
                err_t e = ttosubnet(prefixvalue, strlen(prefixvalue),
                                    AF_INET6, &prefix);
                if(e!=NULL) {
                    fprintf(stderr, "Invalid prefix: %s\n",prefixvalue);
                    usage();
                }
                check_dag(instanceID, dn);
                dn->set_prefix(prefix);
            }
            break;

        case 't':
            if(inet_pton(AF_INET6, optarg, &target)!=1) {
                fprintf(stderr, "Invalid ipv6 address: %s\n", optarg);
                usage();
            }
            dest = &target;
            destnode = new rpl_node(target, dn, deb);
            break;

        case 'M':
            if(inet_pton(AF_INET6, optarg, &myid)!=1) {
                fprintf(stderr, "Invalid ipv6 address in --myid: %s\n", optarg);
                usage();
            }
            dag_me = new prefix_node(deb, myid, 128);
            check_dag(instanceID, dn);
            dn->dag_me = dag_me;
            break;

        case 'v':
            verbose++;
            deb = new rpl_debug(verbose, stderr);
            break;

        case '?':
        case 'h':
        default:
            usage();
            break;
        }
    }

    if(datafilename!=NULL && icmp_len > 0) {
        /* nothing for now */
    } else if(daoack) {
        icmp_len = dn->build_daoack(icmp_body, sizeof(icmp_body), daoack);
    } else if(prefixvalue) {
        fprintf(stderr, "building DAO\n");
        icmp_len = dn->build_dao(icmp_body, sizeof(icmp_body));
    } else {
        fprintf(stderr, "either a prefix is needed to send a DAO, or --daoack\n");
        usage();
    }

    if(icmp_len == 0) {
        fprintf(stderr, "length of generated packet is 0, none sent\n");
        if(!prefixvalue) {
            fprintf(stderr, "No prefix value set\n");
        }
        usage();
        exit(1);
    }

    if(verbose) {
        printf("Sending ICMP of length: %u\n", icmp_len);
        if(icmp_len > 0) {
            hexdump(icmp_body, 0, icmp_len);
        }
    }

    if(iface != NULL && icmp_len > 0) {
        if(piface != NULL) {
            piface->send_raw_icmp(dest, icmp_body, icmp_len);
        } else {
            iface->send_raw_icmp(dest, icmp_body, icmp_len);
        }
    }

    exit(0);
}

/*
 * Local Variables:
 * c-basic-offset:4
 * c-style: whitesmith
 * End:
 */
