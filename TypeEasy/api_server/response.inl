/* Copyright (c) 2013-2017 the Civetweb developers
 * Copyright (c) 2004-2013 Sergey Lyubka
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT- LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/* This file contains an implementation of the response sending functions.
 * It is included in civetweb.c, and does not need to be compiled otherwise.
 */


int
mg_response_header_start(struct mg_connection *conn, int status)
{
	if (!conn) {
		return -1;
	}
	if (conn->connection_type != CONNECTION_TYPE_REQUEST) {
		return -2;
	}
	if (conn->request_state != 0) {
		return -3;
	}

	conn->response_info.status_code = status;
	conn->response_info.status_text = mg_get_response_code_text(conn, status);
	conn->response_info.http_version = "1.1";
	conn->response_info.num_headers = 0;
	conn->request_state = 1;

	return 0;
}


int
mg_response_header_add(struct mg_connection *conn,
                       const char *header,
                       const char *value,
                       int value_len)
{
	if (!conn) {
		return -1;
	}
	if (conn->connection_type != CONNECTION_TYPE_REQUEST) {
		return -2;
	}
	if (conn->request_state != 1) {
		return -3;
	}
	if (conn->response_info.num_headers >= (int)MG_MAX_HEADERS) {
		return -4;
	}

	if (value_len < 0) {
		value_len = (int)strlen(value);
	}

	conn->response_info.http_headers[conn->response_info.num_headers].name =
	    header;
	conn->response_info.http_headers[conn->response_info.num_headers].value =
	    value;
	conn->response_info.num_headers++;

	return 0;
}


int
mg_response_header_add_lines(struct mg_connection *conn,
                             const char *http1_headers)
{
	char *h, *e, *n, *v;
	int cnt = 0;

	if (!conn) {
		return -1;
	}
	if (conn->connection_type != CONNECTION_TYPE_REQUEST) {
		return -2;
	}
	if (conn->request_state != 1) {
		return -3;
	}

	h = mg_strdup(http1_headers);
	if (!h) {
		return -5;
	}

	n = h;
	while (n) {
		/* Find the end of the line */
		e = strchr(n, '\n');
		if (e) {
			*e = 0;
			e++;
		}

		/* Find the value */
		v = strchr(n, ':');
		if (v) {
			*v = 0;
			v++;
			while (*v == ' ') {
				v++;
			}
			mg_response_header_add(conn, n, v, -1);
			cnt++;
		}

		/* to next line */
		n = e;
	}

	mg_free(h);
	return cnt;
}


int
mg_response_header_send(struct mg_connection *conn)
{
	char date[64];
	time_t curtime = time(NULL);
	int i;

	if (!conn) {
		return -1;
	}
	if (conn->connection_type != CONNECTION_TYPE_REQUEST) {
		return -2;
	}
	if (conn->request_state != 1) {
		return -3;
	}

	gmt_time_string(date, sizeof(date), &curtime);

	mg_printf(conn,
	          "HTTP/%s %d %s\r\n",
	          conn->response_info.http_version,
	          conn->response_info.status_code,
	          conn->response_info.status_text);

	mg_printf(conn, "Date: %s\r\n", date);

	/* Keep-alive must be set if the client needs it, otherwise the client
	 * may not send a second request. */
	if (conn->protocol_type == PROTOCOL_TYPE_HTTP1) {
		mg_printf(conn,
		          "Connection: %s\r\n",
		          suggest_connection_header(conn));
	}

	for (i = 0; i < conn->response_info.num_headers; i++) {
		mg_printf(conn,
		          "%s: %s\r\n",
		          conn->response_info.http_headers[i].name,
		          conn->response_info.http_headers[i].value);
	}
	mg_printf(conn, "%s", "\r\n");

	conn->request_state = 2;

	return 0;
}