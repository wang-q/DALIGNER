/*******************************************************************************************
 *
 *  Check the structural integrity of .las files
 *
 *  Author:  Gene Myers
 *  Date  :  July 2014
 *
 *******************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "DB.h"
#include "align.h"

static char *Usage = "[-vaS] <src1:db|dam> [ <src2:db|dam> ] <align:las> ...";

#define MEMORY   1000   //  How many megabytes for output buffer

int main(int argc, char *argv[])
{ DAZZ_DB   _db1,  *db1  = &_db1;
  DAZZ_DB   _db2,  *db2  = &_db2;
  int        VERBOSE;
  int        MAP_ORDER;
  int        SORTED;
  int        ISTWO;
  int        status;

  //  Process options

  { int i, j, k;
    int flags[128];

    ARG_INIT("LAcheck")

    j = 1;
    for (i = 1; i < argc; i++)
      if (argv[i][0] == '-')
        switch (argv[i][1])
        { default:
            ARG_FLAGS("vaS")
            break;
        }
      else
        argv[j++] = argv[i];
    argc = j;

    VERBOSE   = flags['v'];
    MAP_ORDER = flags['a'];
    SORTED    = flags['S'];

    if (argc <= 2)
      { fprintf(stderr,"Usage: %s %s\n",Prog_Name,Usage);
        fprintf(stderr,"\n");
        fprintf(stderr,"      -v: Verbose mode, output error messages.\n");
        fprintf(stderr,"      -S: Check that .las is in sorted order.\n");
        fprintf(stderr,"      -a: If -S, then check sorted by A-read, A-position pairs\n");
        fprintf(stderr,"          off => check sorted by A,B-read pairs (LA-piles)\n");
        exit (1);
      }
  }

  //  Open trimmed DB

  { Block_Looper *parse;
    int   status;

    ISTWO  = 0;
    status = Open_DB(argv[1],db1);
    if (status < 0)
      exit (1);
    if (db1->part > 0)
      { fprintf(stderr,"%s: Cannot be called on a block: %s\n",Prog_Name,argv[1]);
        exit (1);
      }

    if (argc <= 3)
      db2 = db1;
    else
      { parse = Parse_Block_LAS_Arg(argv[2]);
        if (! Next_Block_Exists(parse))
          { ISTWO = 1;
            status = Open_DB(argv[2],db2);
            if (status < 0)
              exit (1);
            if (db2->part > 0)
              { fprintf(stderr,"%s: Cannot be called on a block: %s\n",Prog_Name,argv[2]);
                exit (1);
              }
            Trim_DB(db2);
          }
        else
          db2 = db1;
        Free_Block_Arg(parse);
      }
    Trim_DB(db1);
  }

  { char      *iblock;
    int64      bsize, ovlsize, ptrsize;
    int        i, j;
    DAZZ_READ *reads1  = db1->reads;
    int        nreads1 = db1->nreads;
    DAZZ_READ *reads2  = db2->reads;
    int        nreads2 = db2->nreads;

    //  Setup IO buffers

    ptrsize = sizeof(void *);
    ovlsize = sizeof(Overlap) - ptrsize;
    bsize   = MEMORY * 1000000ll;
    iblock  = (char *) Malloc(bsize+ptrsize,"Allocating input block");
    if (iblock == NULL)
      exit (1);
    iblock += ptrsize;

    //  For each file do

    status = 0;
    for (i = 2+ISTWO; i < argc; i++)
      { Block_Looper *parse;
        FILE     *input;
        char     *disp;
        char     *iptr, *itop;
        Overlap   last, prev;
        int64     novl;
        int       tspace, tbytes;
        int       has_chains;

        //  Establish IO and (novl,tspace) header

        parse = Parse_Block_LAS_Arg(argv[i]);

        while ((input = Next_Block_Arg(parse)) != NULL)
          { disp = Block_Arg_Root(parse);

            if (fread(&novl,sizeof(int64),1,input) != 1)
              SYSTEM_READ_ERROR
            if (fread(&tspace,sizeof(int),1,input) != 1)
              SYSTEM_READ_ERROR
            if (novl < 0)
              { if (VERBOSE)
                  fprintf(stderr,"  %s: Number of alignments < 0\n",disp);
                goto error;
              }
            if (tspace < 0)
              { if (VERBOSE)
                  fprintf(stderr,"  %s: Trace spacing < 0\n",disp);
                goto error;
              }

            if (tspace <= TRACE_XOVR && tspace != 0)
              tbytes = sizeof(uint8);
            else
              tbytes = sizeof(uint16);

            iptr = iblock;
            itop = iblock + fread(iblock,1,bsize,input);

            //  For each record in file do

            has_chains = 0;
            last.aread = -1;
            last.bread = -1;
            last.flags =  0;
            last.path.bbpos = last.path.abpos = 0;
            last.path.bepos = last.path.aepos = 0;
            prev = last;
            for (j = 0; j < novl; j++)
              { Overlap ovl;
                int     tsize;
                int     equal;

                //  Fetch next record

                if (iptr + ovlsize > itop)
                  { int64 remains = itop-iptr;
                    if (remains > 0)
                      memmove(iblock,iptr,remains);
                    iptr  = iblock;
                    itop  = iblock + remains;
                    itop += fread(itop,1,bsize-remains,input);
                    if (iptr + ovlsize > itop)
                      { if (VERBOSE)
                          fprintf(stderr,"  %s: Too few alignment records\n",disp);
                        goto error;
                      }
                  }

                ovl   = *((Overlap *) (iptr - ptrsize));
                iptr += ovlsize;
                tsize = ovl.path.tlen*tbytes;

                if (iptr + tsize > itop)
                  { int64 remains = itop-iptr;
                    if (remains > 0)
                      memmove(iblock,iptr,remains);
                    iptr  = iblock;
                    itop  = iblock + remains;
                    itop += fread(itop,1,bsize-remains,input);
                    if (iptr + tsize > itop)
                      { if (VERBOSE)
                          fprintf(stderr,"  %s: Too few alignment records\n",disp);
                        goto error;
                      }
                  }
                ovl.path.trace = iptr;
                iptr += tsize;

                //  Basic checks

                if (ovl.aread < 0 || ovl.bread < 0)
                  { if (VERBOSE)
                      fprintf(stderr,"  %s: Read indices < 0\n",disp);
                    goto error;
                  }
                if (ovl.aread >= nreads1 || ovl.bread >= nreads2)
                  { if (VERBOSE)
                      fprintf(stderr,"  %s: Read indices out of range\n",disp);
                    goto error;
                  }

                if (ovl.path.abpos >= ovl.path.aepos || ovl.path.aepos > reads1[ovl.aread].rlen ||
                    ovl.path.bbpos >= ovl.path.bepos || ovl.path.bepos > reads2[ovl.bread].rlen ||
                    ovl.path.abpos < 0               || ovl.path.bbpos < 0                       )
                  { if (VERBOSE)
                      fprintf(stderr,"  %s: Non-sense alignment intervals\n",disp);
                    goto error;
                  }

                if (ovl.path.diffs < 0 || ovl.path.diffs > reads1[ovl.aread].rlen ||
                                          ovl.path.diffs > reads2[ovl.bread].rlen)
                  { if (VERBOSE)
                      fprintf(stderr,"  %s: Non-sense number of differences\n",disp);
                    goto error;
                  }

                if (Check_Trace_Points(&ovl,tspace,VERBOSE,disp))
                  goto error;

                if (j == 0)
                  has_chains = ((ovl.flags & (START_FLAG | NEXT_FLAG | BEST_FLAG)) != 0);
                if (has_chains)
                  { if (CHAIN_START(ovl.flags) && CHAIN_NEXT(ovl.flags))
                      { if (VERBOSE)
                          fprintf(stderr,"  %s: LA has both start & next flag set\n",disp);
                        goto error;
                      }
                    if (BEST_CHAIN(ovl.flags) && CHAIN_NEXT(ovl.flags))
                      { if (VERBOSE)
                          fprintf(stderr,"  %s: LA has both best & next flag set\n",disp);
                        goto error;
                      }
                  }
                else
                  { if ((ovl.flags & (START_FLAG | NEXT_FLAG | BEST_FLAG)) != 0)
                      { if (VERBOSE)
                          fprintf(stderr,"  %s: LAs should not have chain flags\n",disp);
                        goto error;
                      }
                  }

                //  Duplicate check and sort check if -S set

                equal = 0;
                if (SORTED)
                  { if (CHAIN_NEXT(ovl.flags))
                      { if (ovl.aread == last.aread && ovl.bread != last.bread &&
                            COMP(ovl.flags) != COMP(last.flags) &&
                            ovl.path.abpos >= last.path.abpos &&
                            ovl.path.bbpos >= last.path.bbpos)
                          goto dupcheck;
                        if (VERBOSE)
                          fprintf(stderr,"  %s: Chain is not valid (%d vs %d)\n",
                                         disp,ovl.aread+1,ovl.bread+1);
                        goto error;
                      }
                    else if (!has_chains)
                      { if (ovl.aread > last.aread) goto inorder;
                        if (ovl.aread == last.aread)
                          { if (MAP_ORDER)
                              { if (ovl.path.abpos > prev.path.abpos) goto inorder;
                                if (ovl.path.abpos == prev.path.abpos)
                                  goto dupcheck;
                              }
                            else
                              { if (ovl.bread > last.bread) goto inorder;
                                if (ovl.bread == last.bread)
                                  { if (COMP(ovl.flags) > COMP(last.flags)) goto inorder;
                                    if (COMP(ovl.flags) == COMP(last.flags))
                                      { if (ovl.path.abpos > last.path.abpos) goto inorder;
                                        if (ovl.path.abpos == last.path.abpos)
                                          { equal = 1;
                                            goto inorder;
                                          }
                                      }
                                  }
                              }
                          }
                        if (VERBOSE)
                          fprintf(stderr,"  %s: LAs are not sorted (%d vs %d)\n",
                                         disp,ovl.aread+1,ovl.bread+1);
                        goto error;
                      }
                    else //  First element of a chain
                      { if (ovl.aread > prev.aread) goto inorder;
                        if (ovl.aread == prev.aread)
                          { if (MAP_ORDER)
                              { if (ovl.path.abpos > prev.path.abpos) goto inorder;
                                if (ovl.path.abpos == prev.path.abpos)
                                  goto dupcheck;
                              }
                            else
                              { if (ovl.bread > prev.bread) goto inorder;
                                if (ovl.bread == prev.bread)
                                  { if (COMP(ovl.flags) > COMP(prev.flags)) goto inorder;
                                    if (COMP(ovl.flags) == COMP(prev.flags))
                                      { if (ovl.path.abpos > prev.path.abpos) goto inorder;
                                        if (ovl.path.abpos == prev.path.abpos)
                                          { equal = 1;
                                            goto dupcheck;
                                          }
                                      }
                                  }
                              }
                          }
                        if (VERBOSE)
                          fprintf(stderr,"  %s: Chains are not sorted (%d vs %d)\n",
                                         disp,ovl.aread+1,ovl.bread+1);
                        goto error;
                      }
                  }
              dupcheck:
                if (ovl.aread == last.aread && ovl.bread == last.bread &&
                    COMP(ovl.flags) == COMP(last.flags) && ovl.path.abpos == last.path.abpos)
                  equal = 1;
              inorder:
                if (equal)
                  { if (ovl.path.aepos == last.path.aepos &&
                        ovl.path.bbpos == last.path.bbpos &&
                        ovl.path.bepos == last.path.bepos)
                      { if (VERBOSE)
                          fprintf(stderr,"  %s: Duplicate LAs (%d vs %d)\n",
                                         disp,ovl.aread+1,ovl.bread+1);
                        goto error;
                      }
                  }

                last = ovl;
                if (CHAIN_START(ovl.flags))
                  prev = ovl;
              }

            //  File processing epilog: Check all data read and print OK if -v

            if (iptr < itop)
              { if (VERBOSE)
                  fprintf(stderr,"  %s: Too many alignment records\n",disp);
                goto error;
              }

            if (VERBOSE)
              { printf("  %s: ",disp);
                Print_Number(novl,0,stdout);
                printf(" all OK\n");
                fflush(stdout);
              }
            goto cleanup;

          error:
            status = 1;
            if (VERBOSE)
              { printf("  %s: Not OK, see stderr\n",disp);
                fflush(stdout);
              }
          cleanup:
            if (input != NULL)
              fclose(input);
          }

        Free_Block_Arg(parse);
      }

    free(iblock-ptrsize);
  }

  Close_DB(db1);
  if (ISTWO)
    Close_DB(db2);

  exit (status);
}
