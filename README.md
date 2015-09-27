First patch use:

        patch -p1 < <your patch>

Patch using interdiff:

        interdiff -z <your old patch> <your new patch> | patch -p1
 

Build command:

        echo make_kernel.sh
        gedit make_kernel.sh

-write in that file

        make <your config>
        make -j<number of your computer cpu cores>

-save

-command

        ./make_kernel.sh
 

Modules

        mkdir -p modules
        find . -name '*ko' -exec cp '{}' modules \;


Clean Source

        make clean
        make mrproper
