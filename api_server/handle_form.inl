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
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/*
 * This file contains an implementation of the mg_handle_form_request function.
 * It is included in civetweb.c, and does not need to be compiled otherwise.
 */


struct mg_posted_file {
	const char *filename;
	const char *path;
	long long size;
	int _reserved;
};


struct mg_form_data_handler_struct {
	struct mg_connection *conn;
	void *user_data;
	int (*field_found)(const char *key,
	                   const char *filename,
	                   char *path,
	                   size_t pathlen,
	                   void *user_data);
	int (*field_get)(const char *key,
	                 const char *value,
	                 size_t valuelen,
	                 void *user_data);
	int (*field_store)(const char *path, long long file_size, void *user_data);
};


static int
mg_get_boundary(struct mg_connection *conn, char *buf, int buf_len)
{
	const char *content_type;
	int ret = -1;

	content_type = mg_get_header(conn, "Content-Type");
	if (content_type != NULL) {
		if (sscanf(content_type,
		           "multipart/form-data; boundary=%s",
		           buf) // NOLINT(cert-err34-c)
		    == 1) {
			/* Boundary is found, success */
			ret = (int)strlen(buf);
		} else {
			/* Try with quotes */
			if (sscanf(content_type,
			           "multipart/form-data; boundary=\"%[^\"]\"",
			           buf) // NOLINT(cert-err34-c)
			    == 1) {
				/* Boundary is found, success */
				ret = (int)strlen(buf);
			}
		}
	}

	if (ret < 0) {
		/* Content-Type is not multipart/form-data or boundary is not found */
		return -1;
	}

	/* Add starting boundary marker */
	memmove(buf + 2, buf, ret);
	buf[0] = '-';
	buf[1] = '-';
	ret += 2;
	buf[ret] = 0;

	(void)buf_len;

	return ret;
}


static int
mg_read_body(struct mg_connection *conn, char *buf, int len)
{
	int ret = 0;

	if (conn) {
		ret = mg_read(conn, buf, (size_t)len);
	}
	return ret;
}


static int
mg_find_boundary(struct mg_connection *conn,
                 const char *boundary,
                 char *buf,
                 int buf_len,
                 int *body_len,
                 int *head_len,
                 int *bound_len)
{
	int blen = (int)strlen(boundary);
	int i, len, hlen, past_header;
	int ret;

	len = *body_len;
	hlen = *head_len;
	past_header = (hlen > 0);

	i = 0;
	while (i < (len - blen - 3)) {
		if ((buf[i] == boundary[0]) && (!memcmp(buf + i, boundary, blen))) {
			*bound_len = blen;
			return i;
		}
		i++;
	}

	while (!past_header) {
		/* find header */
		while (len < buf_len) {
			ret = mg_read_body(conn, buf + len, buf_len - len);
			if (ret <= 0) {
				return -1;
			}
			len += ret;

			hlen = get_http_header_len(buf, len);
			if (hlen > 0) {
				past_header = 1;
				break;
			}
		}
		if (hlen <= 0) {
			return -1;
		}
		*body_len = len;
		*head_len = hlen;
	}

	while (len < buf_len) {
		ret = mg_read_body(conn, buf + len, buf_len - len);
		if (ret <= 0) {
			return -1;
		}
		len += ret;

		i = 0;
		while (i < (len - blen - 3)) {
			if ((buf[i] == boundary[0]) && (!memcmp(buf + i, boundary, blen))) {
				*bound_len = blen;
				return i;
			}
			i++;
		}
	}

	return -1;
}


static int
mg_handle_form_single_field(struct mg_connection *conn,
                            const char *boundary,
                            char *buf,
                            int buf_len,
                            int *body_len,
                            int *head_len,
                            int *bound_len,
                            struct mg_form_data_handler_struct *fdh)
{
	const char *key, *filename;
	char path[2048];
	FILE *f = NULL;
	int64_t file_size = 0;
	int i, len, hlen, blen, get_len, store_len;
	int field_storage;
	int ret;

	len = *body_len;
	hlen = *head_len;
	blen = *bound_len;

	key = mg_get_header(conn, "Content-Disposition");
	if (key == NULL) {
		return -1;
	}

	if (sscanf(key, "form-data; name=\"%[^\"]\"", path) != 1) {
		return -1;
	}
	key = path;

	if (sscanf(mg_get_header(conn, "Content-Disposition"),
	           "form-data; name=\"%*[^\"]\"; filename=\"%[^\"]\"",
	           path)
	    == 1) {
		filename = path;
	} else {
		filename = NULL;
	}

	path[0] = 0;
	field_storage =
	    fdh->field_found(key, filename, path, sizeof(path), fdh->user_data);

	if ((field_storage & MG_FORM_FIELD_STORAGE_ABORT) != 0) {
		return -1;
	}

	if (field_storage == MG_FORM_FIELD_STORAGE_STORE) {
		if (path[0]) {
			f = fopen(path, "wb");
			if (!f) {
				return -1;
			}
		} else {
			/* no path specified */
			field_storage = MG_FORM_FIELD_STORAGE_SKIP;
		}
	}

	memmove(buf, buf + hlen, len - hlen);
	len -= hlen;
	hlen = 0;

	i = mg_find_boundary(conn, boundary, buf, buf_len, &len, &hlen, &blen);

	if (i < 0) {
		/* error */
		if (f) {
			fclose(f);
		}
		return -1;
	}

	get_len = i;
	store_len = i;

	if ((get_len > 1) && (buf[get_len - 2] == '\r')
	    && (buf[get_len - 1] == '\n')) {
		get_len -= 2;
	}
	if ((store_len > 1) && (buf[store_len - 2] == '\r')
	    && (buf[store_len - 1] == '\n')) {
		store_len -= 2;
	}

	if (field_storage == MG_FORM_FIELD_STORAGE_GET) {
		ret = fdh->field_get(key, buf, (size_t)get_len, fdh->user_data);
		if ((ret & MG_FORM_FIELD_HANDLE_ABORT) != 0) {
			if (f) {
				fclose(f);
			}
			return -1;
		}
	}
	if (field_storage == MG_FORM_FIELD_STORAGE_STORE) {
		if (f) {
			if (fwrite(buf, 1, store_len, f) != (size_t)store_len) {
				fclose(f);
				return -1;
			}
			file_size += store_len;
		}
	}

	memmove(buf, buf + i, len - i);
	len -= i;

	while (field_storage == MG_FORM_FIELD_STORAGE_STORE) {
		i = mg_find_boundary(conn, boundary, buf, buf_len, &len, &hlen, &blen);

		if (i < 0) {
			/* error */
			if (f) {
				fclose(f);
			}
			return -1;
		}

		store_len = i;
		if ((store_len > 1) && (buf[store_len - 2] == '\r')
		    && (buf[store_len - 1] == '\n')) {
			store_len -= 2;
		}

		if (f) {
			if (fwrite(buf, 1, store_len, f) != (size_t)store_len) {
				fclose(f);
				return -1;
			}
			file_size += store_len;
		}

		memmove(buf, buf + i, len - i);
		len -= i;
	}

	if (f) {
		fclose(f);
		f = NULL;
		ret = fdh->field_store(path, file_size, fdh->user_data);
		if ((ret & MG_FORM_FIELD_HANDLE_ABORT) != 0) {
			return -1;
		}
	}

	*body_len = len;
	*head_len = hlen;
	*bound_len = blen;

	return 0;
}


CIVETWEB_API int
mg_handle_form_request(struct mg_connection *conn,
                       struct mg_form_data_handler *fdh)
{
	char buf[16 * 1024];
	char boundary[256];
	int head_len, body_len, bound_len;
	int ret;
	int fields = 0;
	struct mg_form_data_handler_struct fdh_struct;

	if (fdh == NULL) {
		return -1;
	}

	/* Get boundary from content-type header */
	if (mg_get_boundary(conn, boundary, sizeof(boundary)) < 0) {
		return -1;
	}

	fdh_struct.conn = conn;
	fdh_struct.user_data = fdh->user_data;
	fdh_struct.field_found = fdh->field_found;
	fdh_struct.field_get = fdh->field_get;
	fdh_struct.field_store = fdh->field_store;

	/* Read data from the client */
	body_len = mg_read_body(conn, buf, sizeof(buf));
	if (body_len <= 0) {
		return -1;
	}

	/* In the first part, we expect a boundary, but no headers */
	head_len = 0;

	/* Find the first boundary */
	ret = mg_find_boundary(
	    conn, boundary, buf, sizeof(buf), &body_len, &head_len, &bound_len);
	if (ret < 0) {
		return -1;
	}

	/* Remove boundary from the buffer */
	memmove(buf, buf + ret + bound_len, body_len - ret - bound_len);
	body_len = body_len - ret - bound_len;

	/* Boundary may be followed by "--" */
	if ((body_len >= 2) && (buf[0] == '-') && (buf[1] == '-')) {
		/* This is the end of the form data */
		return 0;
	}

	/* Or boundary is followed by \r\n */
	if ((body_len >= 2) && (buf[0] == '\r') && (buf[1] == '\n')) {
		memmove(buf, buf + 2, body_len - 2);
		body_len -= 2;
	} else {
		/* This is not a valid boundary */
		return -1;
	}

	for (;;) {
		/* Every field starts with a header */
		head_len = get_http_header_len(buf, body_len);
		if (head_len <= 0) {
			/* header not found */
			/* maybe we need to read more data */
			if (body_len < (int)sizeof(buf)) {
				ret = mg_read_body(conn, buf + body_len, sizeof(buf) - body_len);
				if (ret > 0) {
					body_len += ret;
					head_len = get_http_header_len(buf, body_len);
				}
			}
		}

		if (head_len <= 0) {
			/* Still no header found */
			return -1;
		}

		/* Parse header and store it in conn */
		if (parse_http_headers(&buf, conn->request_info.http_headers) < 0) {
			/* Error parsing headers */
			return -1;
		}

		ret = mg_handle_form_single_field(conn,
		                                  boundary,
		                                  buf,
		                                  sizeof(buf),
		                                  &body_len,
		                                  &head_len,
		                                  &bound_len,
		                                  &fdh_struct);

		if (ret < 0) {
			return fields;
		}
		fields++;

		/* Check end of data */
		if ((body_len >= 2) && (buf[0] == '-') && (buf[1] == '-')) {
			/* This is the end of the form data */
			break;
		}

		/* Or boundary is followed by \r\n */
		if ((body_len >= 2) && (buf[0] == '\r') && (buf[1] == '\n')) {
			memmove(buf, buf + 2, body_len - 2);
			body_len -= 2;
		} else {
			/* This is not a valid boundary */
			break;
		}
	}

	return fields;
}