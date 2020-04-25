/*
 * Copyright (C) 2013 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/time.h>
#include <math.h>
#include <time.h>
#include <omp.h>

#include "ch-placement-oid-gen.h"
#include "ch-placement.h"
#ifdef CH_ENABLE_CRUSH 
#include "ch-placement-crush.h"
#endif
#include "comb.h"

struct options
{
    unsigned int num_servers;
    unsigned int num_devices;
    unsigned int num_objs;
    unsigned int replication;
    char *placement;
    unsigned int virt_factor;
    unsigned block_size;
    unsigned int sector_size;
    unsigned int threads;
};

struct comb_stats
{
    unsigned long count;
    unsigned long bytes;
};



struct device_type{
    int idex;
    int count;
    double cap;
    double remain;
    double bandwidth;
    double workload;
    double latency;
    long int endura;
};
struct node_type{
    int idex;
    int count;
    int device1_count;                        
    int device2_count;
    int device3_count;
    int num_device;
    double cap;
    double remain;
    double perform;
    double workload;
    struct device_type *media;
};
const struct device_type DEVICE[3] = {
        {0,0,
         1000000000000,                         //Byte
         1000000000000,
         96000000,                             //Bytes/s
         0,
         4200,                                 //μs
         1<<50
        },
        {0,0,
         200000000000,
         200000000000,
         228000000,
         0,
         60,
         1<<20
        },
        {0,0,
         32000000000,
         32000000000,
         2100000000,
         0,
         12,
         1<<40
        }
    };
//




static int comb_cmp(const void *a, const void *b);
static int usage(char *exename);
static struct options *parse_args(int argc, char *argv[]);


#ifdef CH_ENABLE_CRUSH
#include <hash.h>
static int setup_crush(struct options *ig_opts,
                       struct crush_map **map, __u32 **weight, int *n_weight)
{
    struct crush_bucket *bucket;
    int i;
    int *items;
    int *weights;
    int ret;
    int id;
    struct crush_rule *rule;

    *n_weight = ig_opts->num_servers;

    *weight = malloc(sizeof(**weight) * ig_opts->num_servers);
    weights = malloc(sizeof(*weights) * ig_opts->num_servers);
    items = malloc(sizeof(*items) * ig_opts->num_servers);
    if (!(*weight) || !weights || !items || !map)
    {
        return (-1);
    }

    for (i = 0; i < ig_opts->num_servers; i++)
    {
        items[i] = i;
        weights[i] = 0x10000;
        (*weight)[i] = 0x10000;
    }

    *map = crush_create();
    assert(*map);
    if (strcmp(ig_opts->placement, "crush-vring") == 0)
#ifdef CH_ENABLE_CRUSH_VRING
        bucket = crush_make_bucket(*map, CRUSH_BUCKET_VRING, CRUSH_HASH_DEFAULT, 1,
                                   ig_opts->num_servers, items, weights);
#else
        assert(0);
#endif
    else
        bucket = crush_make_bucket(*map, CRUSH_BUCKET_STRAW, CRUSH_HASH_DEFAULT, 1,
                                   ig_opts->num_servers, items, weights);
    assert(bucket);

    ret = crush_add_bucket(*map, -2, bucket, &id);
    assert(ret == 0);

    crush_finalize(*map);

    rule = crush_make_rule(3, 0, 1, 1, 10);
    assert(rule);

    crush_rule_set_step(rule, 0, CRUSH_RULE_TAKE, id, 0);
    crush_rule_set_step(rule, 1, CRUSH_RULE_CHOOSELEAF_FIRSTN, 8, 0);
    crush_rule_set_step(rule, 2, CRUSH_RULE_EMIT, 0, 0);

    ret = crush_add_rule(*map, rule, 0);
    assert(ret == 0);

    return (0);
}
#endif





/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*    gen         */
void server_gen(int num_servers ,int num_devices, struct node_type** server){
    unsigned int i;
    int r = 0;
    *server = malloc(num_servers*sizeof(**server));
    assert(*server);
    for(i=0; i<num_servers;i++){
        r = rand() % 13;
        (*server)[i].idex = i;
        (*server)[i].count = 0;
        (*server)[i].num_device = num_devices;         
        (*server)[i].cap = 0;
        (*server)[i].remain = 0;
        (*server)[i].perform = 0;
        (*server)[i].workload = 0;
        (*server)[i].device1_count = 0;          
        (*server)[i].device2_count = 0;
        (*server)[i].device3_count = 0;
    }
    return;    
}


void device_gen(struct node_type server_i , struct device_type** device){
    unsigned int i;
    int r = 0;
    *device = malloc(server_i.num_device*sizeof(**device));
    assert(*device);
    for(i=0;i<server_i.num_device;i++){
        
        r = rand() % 3;
        (*device)[i].idex = i;
        (*device)[i].count = 0;
        (*device)[i].cap = DEVICE[r].cap;
        (*device)[i].remain = DEVICE[r].remain;
        (*device)[i].bandwidth = DEVICE[r].bandwidth;
        (*device)[i].workload = DEVICE[r].workload;
        (*device)[i].latency = DEVICE[r].latency;
        (*device)[i].endura = DEVICE[r].endura;
    }
    return;

}




int server_choose(int i, int window, int num_server,struct node_type *server,double bsize){
    int j =0;
    int k =0;
    double blocksize = bsize*1000,max = 0;                       
    double a[window],b[window],c[window],d[window];
    for(k = 0;k<window;k++){
        a[k] = server[(i+k)%num_server].cap * server[(i+k)%num_server].perform;
        b[k] = server[(i+k)%num_server].remain;
        c[k] = server[(i+k)%num_server].perform - server[(i+k)%num_server].workload;
    }
    for(k = 0;k<window;k++){                                   
        d[k] = ((b[k]-blocksize)*(c[k]-blocksize/10000)*a[k]+b[(k+1)%window]*c[(k+1)%window]*a[(k+1)%window]+b[(k+2)%window]*c[(k+2)%window]*a[(k+2)%window] )
                / sqrt(((b[k]-blocksize)*(c[k]-blocksize/10000)*(b[k]-blocksize)*(c[k]-blocksize/10000)
                  +b[(k+1)%window]*c[(k+1)%window]*b[(k+1)%window]*c[(k+1)%window]
                  +b[(k+2)%window]*c[(k+2)%window]*b[(k+2)%window]*c[(k+2)%window])*(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]));
        
        if(d[k]>max){
            max = d[k];
            j = k;
        }
    }
    return i+j;
}

int device_choose(int i, int window,int num_device, struct device_type *device,double bsize){
    int j = 0;
    int k = 0;
    double blocksize = bsize*1000,max = 0;                           
    double a[window];
    double b[window];
    double c[window];
    double d[window];   
    for(k = 0;k<window;k++){                                
        a[k] = device[(i+k)%num_device].cap * device[(i+k)%num_device].bandwidth / device[(i+k)%num_device].latency;  
        b[k] = device[(i+k)%num_device].remain;
        c[k] = (device[(i+k)%num_device].bandwidth - device[(i+k)%num_device].workload) / device[(i+k)%num_device].latency;

    } 
    for(k = 0;k<window;k++){
        d[k] = ((b[k]-blocksize)*(c[k]-blocksize/10000)*a[k]+b[(k+1)%window]*c[(k+1)%window]*a[(k+1)%window]+b[(k+2)%window]*c[(k+2)%window]*a[(k+2)%window] )
                / sqrt(((b[k]-blocksize)*(c[k]-blocksize/10000)*(b[k]-blocksize)*(c[k]-blocksize/10000)
                  +b[(k+1)%window]*c[(k+1)%window]*b[(k+1)%window]*c[(k+1)%window]
                  +b[(k+2)%window]*c[(k+2)%window]*b[(k+2)%window]*c[(k+2)%window])*(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]));
        
        if(d[k]>max){
            max = d[k];
            j = k;
        }
    }   
    return j+i;
}





//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(
    int argc,
    char **argv)
{
    struct options *ig_opts = NULL;                   
    unsigned long total_byte_count = 0;
    unsigned long total_obj_count = 0;                   
    struct obj *total_objs = NULL;                 

    struct node_type *server = NULL;
    struct device_type *device = NULL;

    unsigned int i,j,k;
    struct ch_placement_instance *instance;      
    struct ch_placement_instance *instance2; 
    int fd;
    struct comb_stats *cs;                  
    
    uint64_t num_combs;
    unsigned long comb_tmp[CH_MAX_REPLICATION];
    unsigned long server_index[CH_MAX_REPLICATION];
    unsigned long device_index_temp[1];
    unsigned long device_index;
    int ret;
//    srand((unsigned)time(NULL));          //random seed
    srand(123);                           //solid seed

    
    struct timeval start1,end1;
    gettimeofday(&start1, NULL );           



#ifdef CH_ENABLE_CRUSH
    struct crush_map *map;
    __u32 *weight;
    int n_weight;
#endif

    ig_opts = parse_args(argc, argv);         // input
    if (!ig_opts)
    {
        usage(argv[0]);
        return (-1);
    }



    if (strcmp(ig_opts->placement, "crush") == 0 ||
        strcmp(ig_opts->placement, "crush-vring") == 0)
    {
#ifdef CH_ENABLE_CRUSH
        ret = setup_crush(ig_opts, &map, &weight, &n_weight);
        if (ret < 0)
        {
            fprintf(stderr, "Error: failed to set up CRUSH.\n");
            return (-1);
        }

        instance = ch_placement_initialize_crush(map, weight, n_weight);
#else
        fprintf(stderr, "Error: not compiled with CRUSH support.\n");
#endif
    }
    else
    {
        instance = ch_placement_initialize(ig_opts->placement,
                                           ig_opts->num_servers,
                                           ig_opts->virt_factor,
                                           0);
    }

    /* generate random set of objects for testing */
    printf("# Generating random object IDs...\n");
    oid_gen("random", instance, ig_opts->num_objs, ULONG_MAX, 
            8675309, ig_opts->replication, ig_opts->num_servers,
            NULL,
            &total_byte_count, &total_obj_count, &total_objs); 





    /*   generate server and device     */
                         
    server_gen(ig_opts->num_servers, ig_opts->num_devices,&server);

    for(i = 0; i < ig_opts->num_servers; i++){
        double sum1 = 0, sum2 = 0;
        device_gen(server[i] ,&device);               
        server[i].media = device;
        for(j = 0;j<server[i].num_device;j++){
             sum1 += device[j].bandwidth;
             sum2 += device[j].cap;
        }
        server[i].perform = (sum1 / (server[i].num_device));
        server[i].cap = sum2;
        server[i].remain = sum2;         
    }                                       
    printf("# Done.\n");
    printf("# Object population consuming approximately %lu MiB of memory.\n", (ig_opts->num_objs * sizeof(*total_objs)) / (1024 * 1024));
    assert(total_obj_count == ig_opts->num_objs);
    sleep(1);
    printf("# Calculating placement for each object ID...\n");


    
    int o = 0;
    double timecost = 0;
    double Time = 0;
    double Time_last = 0;
    struct node_type *server_last = NULL;
    server_last = malloc(ig_opts->num_servers*sizeof(*server_last));
    unsigned int block = (ig_opts->block_size)*1000;                       
    unsigned int THD = ig_opts->threads;
    double incre[THD];       
    int tid = 0;


#pragma omp parallel num_threads(THD)                
{
    for (i = 0; i < ig_opts->num_objs; i++)
    {
        ch_placement_find_closest(instance, total_objs[i].oid, ig_opts->replication, total_objs[i].server_idxs);   //hashing
            memcpy(comb_tmp, total_objs[i].server_idxs,           //hashing result, saved in comb_tmp
                   ig_opts->replication * sizeof(*comb_tmp));
            
/*  hashing   */   
 
        for(j = 0;j<ig_opts->replication;j++){   //添写三个变量两个函数：server_index[]、device_index_temp、device_index
            server_index[j] = server_choose(comb_tmp[j],ig_opts->sector_size,ig_opts->num_servers,server,ig_opts->block_size) % ig_opts->num_servers; 
            server[server_index[j]].count++;                                                      
            server[server_index[j]].remain = server[server_index[j]].remain - block;
            server[server_index[j]].workload = server[server_index[j]].workload + block/10000;     
            
            instance2 = ch_placement_initialize(ig_opts->placement,
                                                server[server_index[j]].num_device,
                                                ig_opts->virt_factor,
                                                0);                                        
            ch_placement_find_closest(instance2, total_objs[i].oid, 1, device_index_temp);//hashing               
            device_index = device_choose(device_index_temp[0],ig_opts->sector_size,server[server_index[j]].num_device, server[server_index[j]].media,ig_opts->block_size) % server[server_index[j]].num_device;
                                 
            tid = omp_get_thread_num();
            incre[tid] = block /(server[server_index[j]].media[device_index].bandwidth);
            
            double yu = 0;
            yu = (i*3+j)%THD;
            if(yu==THD-1){
                double max = 0;
                double sm = 0;
                for(k = 0;k<THD;k++){
                    sm += incre[k];                    
                }
                max = sm/THD;
                Time = Time + max;
        
            }
            server[server_index[j]].media[device_index].count++;
            
            if(server[server_index[j]].media[device_index].latency == 4200){
                server[server_index[j]].device1_count++;
            }
            if(server[server_index[j]].media[device_index].latency == 60){
                server[server_index[j]].device2_count++;
            }
            if(server[server_index[j]].media[device_index].latency == 12){
                server[server_index[j]].device3_count++;
            }
            server[server_index[j]].media[device_index].remain = server[server_index[j]].media[device_index].remain - block;
            server[server_index[j]].media[device_index].workload = server[server_index[j]].media[device_index].workload + block/10000;
        }                                                                                     


        
    }
}



gettimeofday(&end1, NULL );
long timeuse =1000000 * ( end1.tv_sec - start1.tv_sec ) + end1.tv_usec - start1.tv_usec;

double sumsum = 0;               
for(i = 0;i<ig_opts->num_servers;i++){
    sumsum += server[i].cap/1000000000 * server[i].perform/1000000 ;
}

/* result */

    for(i = 0;i<ig_opts->num_servers;i++){
        printf("server_index:%d\n ", i);
        printf("capcity:%.1f GB\n remain:%.1f GB\n perform:%.0lf MBps\n num_device:%.0d\n", server[i].cap/1000000000,server[i].remain/1000000000, server[i].perform/1000000,  server[i].num_device);
        printf("workload:%.1lf MBps\n",server[i].workload/1000000);
        printf("datacount:%d\n",server[i].count);
        printf("device1_count:%d\n",server[i].device1_count);
        printf("device2_count:%d\n",server[i].device2_count);
        printf("device3_count:%d\n",server[i].device3_count);
        
        double use = 0;
        use = (server[i].cap - server[i].remain) / server[i].cap * 100;
        printf("used_rate(%):%.2f\n",use);

        //ideal distribution
    //    double ca = server[i].cap/1000000000;
    //    double pe = server[i].perform/1000000;
    //    double x = ca*pe/sumsum;             //rate_x
    //    double y = ig_opts->num_objs * ig_opts->replication * x;
    //    printf("ideal_count:%.0f\n",y);
        
        //device info if need
        /*        
        for(int j = 0;j<server[i].num_device;j++){
            printf("device index:%d\t device_cap:%.1f GB\t",j,server[i].media[j].cap / 1000000000);
            printf("device remain:%.1f GB\t",server[i].media[j].remain / 1000000000);
            printf("device_bandwidth:%.0lf MBps\t",server[i].media[j].bandwidth / 1000000);
            printf("device_workload:%.1lf\t",server[i].media[j].workload);
            printf("device_latency:%.0f μs\n",server[i].media[j].latency);
            printf("data_count:%d \n",server[i].media[j].count);
        }
      */
        printf("\n"); 
    }
    
    
    printf("total_byte_count:%ld\n total_obj_count:%ld\n",total_byte_count,total_obj_count);
    printf("time_algorithm=%f\n",timeuse /1000000.0);
    printf("time_distribution=%f\n",Time);
    printf("# Done.\n");



    /* we don't need the global list any more */
    free(total_objs);
    total_obj_count = 0;
    total_byte_count = 0;

    return (0);
}


static int usage(char *exename)
{
    fprintf(stderr, "Usage: %s [options]\n", exename);
    fprintf(stderr, "    -s <number of servers>\n");
    fprintf(stderr, "    -d <number of devices>\n");
    fprintf(stderr, "    -o <number of objects>\n");
    fprintf(stderr, "    -r <replication factor>\n");
    fprintf(stderr, "    -p <placement algorithm>\n");
    fprintf(stderr, "    -v <virtual nodes per physical node>\n");
    fprintf(stderr, "    -b <size of block (KB)>\n");
    fprintf(stderr, "    -e <size of sector>\n");
    fprintf(stderr, "    -t <number of threads>\n");
    exit(1);
}

static struct options *parse_args(int argc, char *argv[])
{
    struct options *opts = NULL;
    int ret = -1;
    int one_opt = 0;

    opts = (struct options *)malloc(sizeof(*opts));
    if (!opts)
        return (NULL);
    memset(opts, 0, sizeof(*opts));

    while ((one_opt = getopt(argc, argv, "s:d:o:r:hp:v:b:e:t:")) != EOF)
    {
        switch (one_opt)
        {
        case 's':
            ret = sscanf(optarg, "%u", &opts->num_servers);
            if (ret != 1)
                return (NULL);
            break;
        case 'd':
            ret = sscanf(optarg, "%u", &opts->num_devices);                 
            if (ret != 1)
                return (NULL);
            break;    
        case 'o':
            ret = sscanf(optarg, "%u", &opts->num_objs);
            if (ret != 1)
                return (NULL);
            break;
        case 'v':
            ret = sscanf(optarg, "%u", &opts->virt_factor);
            if (ret != 1)
                return (NULL);
            break;
        case 'r':
            ret = sscanf(optarg, "%u", &opts->replication);
            if (ret != 1)
                return (NULL);
            break;
        case 'b':
            ret = sscanf(optarg, "%u", &opts->block_size);
            if (ret != 1)
                return (NULL);
            break;
        case 'e':
            ret = sscanf(optarg, "%u", &opts->sector_size);
            if (ret != 1)
                return (NULL);
            break; 
        case 't':
            ret = sscanf(optarg, "%u", &opts->threads);
            if (ret != 1)
                return (NULL);
            break;            
        case 'p':
            opts->placement = strdup(optarg);
            if (!opts->placement)
                return (NULL);
            break;
        case '?':
            usage(argv[0]);
            exit(1);
        }
    }

    if (opts->replication < 2)
        return (NULL);
    if (opts->num_servers < (opts->replication + 1))
        return (NULL);
    if (opts->num_devices < 1)
        return (NULL);
    if (opts->num_objs < 1)
        return (NULL);
    if (opts->virt_factor < 1)
        return (NULL);
    if (!opts->placement)
        return (NULL);
    if (opts->num_devices<1)
        return (NULL);
    if (opts->sector_size<3)
        return (NULL);
    if (opts->threads<1)
        return (NULL);                

    assert(opts->replication <= CH_MAX_REPLICATION);

    return (opts);
}

static int comb_cmp(const void *a, const void *b)
{
    unsigned long au = ((struct comb_stats *)a)->count;
    unsigned long bu = ((struct comb_stats *)b)->count;
    int rtn;
    if (au < bu)
        rtn = -1;
    else if (au == bu)
        rtn = 0;
    else
        rtn = 1;
    return rtn;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */

