/**
 * Gmakemake wrapper for the real main. I *hate* gmakemake. Is writing make 
 * files really *that* hard?
 */

extern int rsh_main(int argc, char **argv);

int main(int argc, char **argv){

  return rsh_main(argc, argv);

}
