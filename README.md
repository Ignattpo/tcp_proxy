### TCP Proxy

Реализовать TCP Proxy сервер, который должен принимать подключения по tcp.
Для каждого принятого подключения создается новое соединение (на указанный в командной строке) сервер и порт. Проксирование происходит в обе стороны.

#### Возможный кейс тестирования:

* Запустить сервер tcp_proxy -l 127.0.0.1:3333 -d ya.ru:80
* Выполнить команду echo -e "GET / HTTP/1.1\nHost: ya.ru\n\n"|nc ya.ru 80
* Убедиться, что получен ответ от сервера

Также можно в /etc/hosts прописать домен ya.ru как 127.0.0.1,
что позволит тестировать через curl: curl -L http://ya.ru:3333

