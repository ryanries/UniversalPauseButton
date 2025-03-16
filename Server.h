#pragma once
#include <stdbool.h>

int serve_start(int port);

bool serve_request(bool isPaused);

int serve_stop(void);

int openWelcomePageInBrowser();
