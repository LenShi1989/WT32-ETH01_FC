#pragma once

namespace Display {
void begin();
void showMessage(const char *line1, const char *line2 = nullptr);
void loop();   // 約 10Hz 更新畫面
}
