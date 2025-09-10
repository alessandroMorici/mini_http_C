# mini_http_C

Compilation & usage
	1.	gcc -O2 -Wall -o mini_http mini_http.c
	2.	./mini_http 8080 (ports <1024 require root on Linux)
	3.	Place an index.html in the same folder or request /filename.
	4.	Test with browser or curl:
	•	curl -i http://localhost:8080/
	•	curl -i http://localhost:8080/somefile.png

Suggestions d’améliorations possibles
	•	ajouter fork() ou pthread pour gérer plusieurs clients en parallèle,
	•	gérer Connection: keep-alive,
	•	ajouter support de Range (streaming/partial content),
	•	sécuriser davantage la résolution des chemins et chroot pour confinement,
	•	ajouter log format Common Log Format et rotation,
	•	utiliser sendfile() pour meilleure performance sur fichiers statiques.
