# Makefile - Simulation Magasin de Bricolage

all:
	gcc -Wall -Wextra -c -g ipc.c -o ipc.o
	gcc -Wall -Wextra -c -g log.c -o log.o
	gcc -Wall -Wextra -c -g utils.c -o utils.o
	gcc -Wall -Wextra -c -g main.c -o main.o
	gcc -Wall -Wextra -c -g vendeur.c -o vendeur.o
	gcc -Wall -Wextra -c -g caissier.c -o caissier.o
	gcc -Wall -Wextra -c -g client.c -o client.o
	gcc -Wall -Wextra -c -g monitoring.c -o monitoring.o
	gcc -o main main.o ipc.o log.o utils.o
	gcc -g -o vendeur vendeur.o ipc.o log.o utils.o
	gcc -g -o caissier caissier.o ipc.o log.o utils.o
	gcc -g -o client client.o ipc.o log.o utils.o
	gcc -g -o monitoring monitoring.o ipc.o log.o utils.o

clean:
	rm -f *.o main vendeur caissier client monitoring

clean-ipc:
	ipcrm -a 2>/dev/null || true
	rm -f /tmp/magasin_ipc
