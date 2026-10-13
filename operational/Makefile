.PHONY: rootkit server vm webapp clean

rootkit:
	$(MAKE) -C rootkit

server: webapp
	python3 run_server.py

vm: rootkit
	./boot

webapp:
	@if [ ! -d webapp/dist ]; then \
		cd webapp && npm install && npm run build; \
	fi

clean:
	$(MAKE) -C rootkit clean
	rm -rf .state
