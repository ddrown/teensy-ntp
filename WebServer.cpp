#include <string.h>

#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"

#include "WebServer.h"

WebServer webserver;

void WebServer::begin() {
  httpd_init();
  webpages = NULL;
}

void WebServer::setWebpages(const struct webpage *new_webpages) {
  webpages = new_webpages;
}

const char *WebServer::getPage(const char *name) {
  if(webpages == NULL) {
    return NULL;
  }
  for(uint32_t i = 0; webpages[i].filename != NULL; i++) {
    if(strcmp(name, webpages[i].filename) == 0) {
      if(webpages[i].pageGen) {
        return webpages[i].pageGen(name);
      }
      return webpages[i].content;
    }
  }
  return NULL;
}

extern "C" {
  int fs_open_custom(struct fs_file *file, const char *name) {
    if (file && name) {
      const char *data = webserver.getPage(name);
      if(data) {
        file->data = data;
        file->len = strlen(file->data);
        file->index = file->len;
        return 1;
      }
      return 0;
    }
    return 0;
  }

  void fs_close_custom(struct fs_file *file) {
  }

  int fs_read_custom(struct fs_file *file, char *buffer, int count) {
    return FS_READ_EOF;
  }
}
