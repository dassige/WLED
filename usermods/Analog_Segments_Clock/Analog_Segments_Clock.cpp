#include "wled.h"

/*
 * Usermod for analog clock  Six-Ten-Twelve segments
 */
extern Timezone *tz;

class Analog_Segments_ClockUsermod : public Usermod
{
private:
    static constexpr uint32_t refreshRate = 50; // per second
    static constexpr uint32_t refreshDelay = 1000 / refreshRate;

    // string that are used multiple time (this will save some flash memory)
    static const char _name[];
    static const char _version[];
    struct Segment
    {
        // config
        int16_t firstLed = 0;
        int16_t lastLed = 0;
        int16_t offset = 0;
        boolean isHours = false;
        // runtime
        int16_t size;

        Segment()
        {
            update();
        }

        void validateAndUpdate()
        {
            if (firstLed < 0 || firstLed >= strip.getLengthTotal() ||
                lastLed < firstLed || lastLed >= strip.getLengthTotal())
            {
                *this = {};
                return;
            }

            update();
            if (offset > size)
            {
                offset = 0;
            }
        }

        void update()
        {
            size = lastLed - firstLed + 1;
        }
    };

    // configuration (available in API and stored in flash)
    bool enabled = false;
    uint32_t hourColor = 0x0000FF;
    uint32_t minuteUnitColor = 0x00FF00;
    uint32_t minuteTensColor = 0x00FF00;
    uint32_t secondUnitColor = 0xFF0000;
    uint32_t secondTensColor = 0xFF0000;
    bool blendColors = false;
    uint16_t movingEffect = 0;
    uint16_t markingMode = 0;

    Segment seconds9_Segment;
    Segment seconds5_Segment;
    Segment minutes9_Segment;
    Segment minutes5_Segment;
    Segment hours12_Segment;

    // runtime
    bool initDone = false;
    // test
    uint16_t previousSeconds = 0;
    uint16_t countH = 0;
    uint16_t countTens = 0;

    /*
        lastOverlayDraw is used to implement a refresh mechanism for the clock overlay. Here's how it works:
            1. The loop() function periodically checks if enough time has passed since the last overlay draw.
            2. If it's time to refresh, strip.trigger() is called.
            3. The handleOverlayDraw() function is called to draw the overlay.
            4. At the beginning of handleOverlayDraw(), lastOverlayDraw is updated with the current time.
            5. The cycle repeats.
        */
    uint32_t lastOverlayDraw = 0;

    void validateAndUpdate()
    {
        seconds9_Segment.validateAndUpdate();
        seconds5_Segment.validateAndUpdate();
        minutes9_Segment.validateAndUpdate();
        minutes5_Segment.validateAndUpdate();
        hours12_Segment.validateAndUpdate();
        hours12_Segment.isHours = true;
        if (movingEffect < 0 || movingEffect > 1)
        {
            movingEffect = 0;
        }
        if (markingMode < 0 || markingMode > 1)
        {
            markingMode = 0;
        }
    }

    /**
     * Adjust a given progress value to a led index within a segment.
     * Given a progress value between 0 and 1, this function will return the
     * corresponding led index within the segment. If the progress value is
     * outside the segment, it will be wrapped around.
     * @param  progress  A double value between 0 and 1.
     * @param  segment   A Segment object defining the segment.
     * @return           The led index within the segment corresponding to the
     *                   given progress value.
     */
    int16_t adjustToSegment(double progress, Segment const &segment)
    {
        int16_t led = segment.firstLed + segment.offset + progress - 1;
        return led > segment.lastLed
                   ? led - segment.lastLed - 1 + segment.firstLed
                   : led;
    }

    /**
     * Set a pixel color.
     * If `blendColors` is true, the given color is blended with the current
     * color of the pixel. Otherwise, the pixel color is simply set to the
     * given color.
     * @param n  The index of the pixel to set.
     * @param c  The color to set the pixel to.
     */
    void setPixelColor(uint16_t n, uint32_t c)
    {
        if (!blendColors)
        {
            strip.setPixelColor(n, c);
        }
        else
        {
            uint32_t oldC = strip.getPixelColor(n);
            strip.setPixelColor(n, qadd32(oldC, c));
        }
    }
    void setPixelColorCumulative(uint16_t n, uint32_t c, Segment const &segment)
    {
        if (n >= segment.firstLed + segment.offset)
        {
            for (int16_t i = segment.firstLed + segment.offset; i <= n; ++i)
            {
                setPixelColor(i, c);
            }
        }
        else
        {
            for (int16_t i = segment.firstLed + segment.offset; i <= segment.lastLed; ++i)
            {
                setPixelColor(i, c);
            }
            for (int16_t i = segment.firstLed; i <= n; ++i)
            {
                setPixelColor(i, c);
            }
        }
    }

    /**
     * Convert a color to a hexadecimal string.
     * @param c The color to convert.
     * @return  A string representing the color in hexadecimal format.
     */
    String colorToHexString(uint32_t c)
    {
        char buffer[9];
        sprintf(buffer, "%06X", c);
        return buffer;
    }

    /**
     * Parse a hexadecimal color string and store the result in `c`.
     * @param  s  The string to parse.
     * @param  c  The variable to store the parsed color in.
     * @param  def  The default value to store in `c` if the string is invalid.
     * @return  Whether the string was parsed successfully.
     */
    bool hexStringToColor(String const &s, uint32_t &c, uint32_t def)
    {
        char *ep;
        unsigned long long r = strtoull(s.c_str(), &ep, 16);
        if (*ep == 0)
        {
            c = r;
            return true;
        }
        else
        {
            c = def;
            return false;
        }
    }

    /**
     * An effect that fades the second hand in a sine wave pattern.
     * This function is called for each led in the second hand segment.
     * @param  secondLed  The index of the led to set.
     * @param  time       The current time.
     */
    void movingEffectSineFade(int16_t led, Toki::Time const &time, Segment const &segment, uint32_t c)
    {
        uint32_t ms = time.ms % 1000;
        uint8_t b0 = (cos8_t(ms * 64 / 1000) - 128) * 2;
        setPixelColor(led, gamma32(scale32(c, b0)));
        uint8_t b1 = (sin8_t(ms * 64 / 1000) - 128) * 2;
        if (segment.isHours || (segment.lastLed + segment.offset) != led)
            setPixelColor(inc(led, 1, segment), gamma32(scale32(c, b1)));
    }
    void movingEffectSineFadeCumulative(int16_t led, Toki::Time const &time, Segment const &segment, uint32_t c)
    {
        if (led >= segment.firstLed + segment.offset)
        {
            for (int16_t i = segment.firstLed + segment.offset; i <= led - 1; ++i)
            {
                setPixelColor(i, c);
            }
        }
        else
        {
            for (int16_t i = segment.firstLed + segment.offset; i <= segment.lastLed; ++i)
            {
                setPixelColor(i, c);
            }
            for (int16_t i = segment.firstLed; i <= led - 1; ++i)
            {
                setPixelColor(i, c);
            }
        }
        uint32_t ms = time.ms % 1000;
        uint8_t b1 = (sin8_t(ms * 64 / 1000) - 128) * 2;
        //  if (segment.isHours || (segment.lastLed + segment.offset) != led)
        setPixelColor(led, gamma32(scale32(c, b1)));
    }

    /**
     * Add two colors together without overflowing.
     * @param c1  The first color to add.
     * @param c2  The second color to add.
     * @return  The sum of `c1` and `c2`, without any color channel exceeding 255.
     */
    static inline uint32_t qadd32(uint32_t c1, uint32_t c2)
    {
        return RGBW32(
            qadd8(R(c1), R(c2)),
            qadd8(G(c1), G(c2)),
            qadd8(B(c1), B(c2)),
            qadd8(W(c1), W(c2)));
    }

    /**
     * Scale a color by a fractional amount.
     * @param c    The color to scale.
     * @param scale  The amount to scale the color by, as a fraction of 256.
     * @return  The scaled color, with no channel exceeding 255.
     */
    static inline uint32_t scale32(uint32_t c, fract8 scale)
    {
        return RGBW32(
            scale8(R(c), scale),
            scale8(G(c), scale),
            scale8(B(c), scale),
            scale8(W(c), scale));
    }

    /**
     * Decrement a given led index within a segment.
     * This function decrements a given led index by `i` within the segment.
     * If the result would be less than the first led of the segment, it wraps
     * around to the end of the segment.
     * @param  n  The led index to decrement.
     * @param  i  The amount to decrement by.
     * @param  seg  The segment to decrement within.
     * @return     The decremented led index.
     */
    static inline int16_t dec(int16_t n, int16_t i, Segment const &seg)
    {
        return n - seg.firstLed >= i
                   ? n - i
                   : seg.lastLed - seg.firstLed - i + n + 1;
    }

    /**
     * Increment a given led index within a segment.
     * This function increments a given led index by `i` within the segment.
     * If the result would be greater than the last led of the segment, it wraps
     * around to the start of the segment.
     * @param  n  The led index to increment.
     * @param  i  The amount to increment by.
     * @param  seg  The segment to increment within.
     * @return     The incremented led index.
     */
    static inline int16_t inc(int16_t n, int16_t i, Segment const &seg)
    {
        int16_t r = n + i;
        if (r > seg.lastLed)
        {
            return seg.firstLed + n - seg.lastLed;
        }
        return r;
    }

public:
    Analog_Segments_ClockUsermod()
    {
    }

    void setup() override
    {
        initDone = true;
        validateAndUpdate();
    }

    void loop() override
    {
        if (millis() - lastOverlayDraw > refreshDelay)
        {
            strip.trigger();
        }
    }

    /**
     * Called just before every show() (LED strip update frame) after effects have set the colors.
     * Use this to blank out some LEDs or set them to a different color regardless of the set effect mode.
     * Commonly used for custom clocks (Cronixie, 7 segment)
     * The AnalogClock6_10_12Usermod::handleOverlayDraw method is an overridden virtual function that is
     * called before each LED strip update frame to modify LED colors independently of the active effect mode.
     * It handles drawing clock elements such as hour marks, seconds, minutes, and hours, with optional effects like fading seconds,
     *  and includes provisions for future enhancements like seconds trails.
     */
    void handleOverlayDraw() override
    {
        if (!enabled)
        {
            return;
        }

        lastOverlayDraw = millis();
        auto time = toki.getTime();

        int16_t seconds = second(localTime);
        int16_t minutes = minute(localTime);
        int16_t hours = hour(localTime);

        // tens seconds leds (seconds5_Segment segment )
        double seconds5P = ((seconds - (seconds % 10)) / 10);
        // 1 to 9 seconds leds (seconds9_Segment segment )
        double seconds9P = (seconds % 10);
        // tens minutes leds (minutes5_Segment segment )
        double minutes5P = ((minutes - (minutes % 10)) / 10);
        // 1 to 9 minutes leds  (minutes9_Segment segment )
        double minutes9P = (minutes % 10);
        // 12 hrs leds (hours12_Segment segment )
        double hours12P = hours > 12 ? hours - 12 : (hours % 12);

        // tests
        /*
        if (previousSeconds != seconds)
        {
            if (countH < 12)
            {
                countH++;
            }
            else
            {
                countH = 1;
            }
            if (countTens < 5)
            {
                countTens++;
            }
            else
            {
                countTens = 0;
            }
            previousSeconds = seconds;
        }
        hours12P = countH;
        minutes9P = seconds9P;
        seconds5P = countTens;
        minutes5P = countTens;
*/
        /*NOTE
        for the seconds and minutes segments (NOT for the hours segment), when the time value is 0, no led is displayed in the segment
        EXAMPLES :
            Time                      marking mode = 0            marking mode = 1
            11h 05m 30s:
                Unit seconds segment: 0,0,0,0,0,0,0,0,0           0,0,0,0,0,0,0,0,0
                Tens seconds segment: 0,0,X,0,0                   X,X,X,0,0
                Unit minutes segment: 0,0,0,0,X,0,0,0,0           X,X,X,X,X,0,0,0,0
                Tens minutes segment: 0,0,0,0,0                   0,0,0,0,0
                Hours ring segment:   0,0,0,0,0,0,0,0,0,0,X,0     X,X,X,X,X,X,X,X,X,X,0
            03h 23m 45s:
                Unit seconds segment: 0,0,0,0,X,0,0,0,0           X,X,X,X,X,0,0,0,0
                Tens seconds segment: 0,0,0,X,0                   X,X,X,X,0
                Unit minutes segment: 0,0,X,0,0,0,0,0,0           X,X,X,0,0,0,0,0,0
                Tens minutes segment: 0,X,0,0,0                   X,X,0,0,0
                Hours ring segment:   0,0,X,0,0,0,0,0,0,0,0,0     X,X,X,0,0,0,0,0,0,0,0
            21h 00m 12s:
                Unit seconds segment: 0,X,0,0,0,0,0,0,0           X,X,0,0,0,0,0,0,0
                Tens seconds segment: X,0,0,0,0                   X,0,0,0,0
                Unit minutes segment: 0,0,0,0,0,0,0,0,0           0,0,0,0,0,0,0,0,0
                Tens minutes segment: 0,0,0,0,0                   0,0,0,0,0
                Hours ring segment:   0,0,0,0,0,0,0,0,X,0,0,0     X,X,X,X,X,X,X,X,X,0,0,0
        */

        switch (markingMode)
        {
        case 0: // only the current led marking the time
        {
            // seconds effect applies only to the seconds 9 leds segment (unit of seconds)
            int16_t secondLed9 = adjustToSegment(seconds9P, seconds9_Segment);
            if (seconds9P > 0) // check if the led is in the segment
                movingEffect == 0 ? setPixelColor(secondLed9, secondUnitColor) : movingEffectSineFade(secondLed9, time, seconds9_Segment, secondUnitColor);

            int16_t secondLed5 = adjustToSegment(seconds5P, seconds5_Segment);
            if (seconds5P > 0)
                movingEffect == 0 ? setPixelColor(secondLed5, secondTensColor) : movingEffectSineFade(secondLed5, time, seconds5_Segment, secondTensColor);

            int16_t minuteLed9 = adjustToSegment(minutes9P, minutes9_Segment);
            if (minutes9P > 0)
                movingEffect == 0 ? setPixelColor(secondLed5, minuteUnitColor) : movingEffectSineFade(minuteLed9, time, minutes9_Segment, minuteUnitColor);

            int16_t minuteLed5 = adjustToSegment(minutes5P, minutes5_Segment);
            if (minutes5P > 0)
                movingEffect == 0 ? setPixelColor(minuteLed5, minuteTensColor) : movingEffectSineFade(minuteLed5, time, minutes5_Segment, minuteTensColor);

            int16_t hourLed12 = adjustToSegment(hours12P, hours12_Segment);
            movingEffect == 0 ? setPixelColor(hourLed12, hourColor) : movingEffectSineFade(hourLed12, time, hours12_Segment, hourColor);

            break;
        }
        case 1: // all the leds up to the current marking the time (cumulative bars)
        {
            int16_t secondLed9 = adjustToSegment(seconds9P, seconds9_Segment);
            if (seconds9P > 0) // check if the led is in the segment
                movingEffect == 0 ? setPixelColorCumulative(secondLed9, secondUnitColor, seconds9_Segment) : movingEffectSineFadeCumulative(secondLed9, time, seconds9_Segment, secondUnitColor);

            int16_t secondLed5 = adjustToSegment(seconds5P, seconds5_Segment);
            if (seconds5P > 0)
                movingEffect == 0 ? setPixelColorCumulative(secondLed5, secondTensColor, seconds5_Segment) : movingEffectSineFadeCumulative(secondLed5, time, seconds5_Segment, secondTensColor);

            int16_t minuteLed9 = adjustToSegment(minutes9P, minutes9_Segment);
            if (minutes9P > 0)
                movingEffect == 0 ? setPixelColorCumulative(minuteLed9, minuteUnitColor, minutes9_Segment) : movingEffectSineFadeCumulative(minuteLed9, time, minutes9_Segment, minuteUnitColor);

            int16_t minuteLed5 = adjustToSegment(minutes5P, minutes5_Segment);
            if (minutes5P > 0)
                movingEffect == 0 ? setPixelColorCumulative(minuteLed5, minuteTensColor, minutes5_Segment) : movingEffectSineFadeCumulative(minuteLed5, time, minutes5_Segment, minuteTensColor);

            int16_t hourLed12 = adjustToSegment(hours12P, hours12_Segment);
            movingEffect == 0 ? setPixelColorCumulative(hourLed12, hourColor, hours12_Segment) : movingEffectSineFadeCumulative(hourLed12, time, hours12_Segment, hourColor);

            break;
        }
        }
    }

    void addToConfig(JsonObject &root) override
    {
        validateAndUpdate();
        JsonObject top = root.createNestedObject(FPSTR(_name));

        top[F("Overlay Enabled")] = enabled;

        top[F("Unit sec FL")] = seconds9_Segment.firstLed;
        top[F("Unit sec LL")] = seconds9_Segment.lastLed;
        top[F("Unit sec Offset")] = seconds9_Segment.offset;

        top[F("Tens sec FL")] = seconds5_Segment.firstLed;
        top[F("Tens sec LL")] = seconds5_Segment.lastLed;
        top[F("Tens sec Offset")] = seconds5_Segment.offset;

        top[F("Unit min FL")] = minutes9_Segment.firstLed;
        top[F("Unit min LL")] = minutes9_Segment.lastLed;
        top[F("Unit min Offset")] = minutes9_Segment.offset;

        top[F("Tens min FL")] = minutes5_Segment.firstLed;
        top[F("Tens min LL")] = minutes5_Segment.lastLed;
        top[F("Tens min Offset")] = minutes5_Segment.offset;

        top[F("Hours FL")] = hours12_Segment.firstLed;
        top[F("Hours LL")] = hours12_Segment.lastLed;
        top[F("Hours Offset")] = hours12_Segment.offset;

        top[F("H Color")] = colorToHexString(hourColor);
        top[F("M Unit Color")] = colorToHexString(minuteUnitColor);
        top[F("M Tens Color")] = colorToHexString(minuteTensColor);
        top[F("S Unit Color")] = colorToHexString(secondUnitColor);
        top[F("S Tens Color")] = colorToHexString(secondTensColor);
        top[F("Moving Effect")] = movingEffect;
        top[F("Marking Mode")] = markingMode;
        top[F("Blend Colors")] = blendColors;

    }
    bool readFromConfig(JsonObject &root) override
    {
        JsonObject top = root[FPSTR(_name)];
        bool configComplete = !top.isNull();

        String color;
        configComplete &= getJsonValue(top[F("Overlay Enabled")], enabled, false);

        configComplete &= getJsonValue(top[F("Unit sec FL")], seconds9_Segment.firstLed, 0);
        configComplete &= getJsonValue(top[F("Unit sec LL")], seconds9_Segment.lastLed, 8);
        configComplete &= getJsonValue(top[F("Unit sec Offset")], seconds9_Segment.offset, 0);

        configComplete &= getJsonValue(top[F("Tens sec FL")], seconds5_Segment.firstLed, 9);
        configComplete &= getJsonValue(top[F("Tens sec LL")], seconds5_Segment.lastLed, 13);
        configComplete &= getJsonValue(top[F("Tens sec Offset")], seconds5_Segment.offset, 0);

        configComplete &= getJsonValue(top[F("Unit min FL")], minutes9_Segment.firstLed, 14);
        configComplete &= getJsonValue(top[F("Unit min LL")], minutes9_Segment.lastLed, 22);
        configComplete &= getJsonValue(top[F("Unit min Offset")], minutes9_Segment.offset, 0);

        configComplete &= getJsonValue(top[F("Tens min FL")], minutes5_Segment.firstLed, 23);
        configComplete &= getJsonValue(top[F("Tens min LL")], minutes5_Segment.lastLed, 27);
        configComplete &= getJsonValue(top[F("Tens min Offset")], minutes5_Segment.offset, 0);

        configComplete &= getJsonValue(top[F("Hours FL")], hours12_Segment.firstLed, 28);
        configComplete &= getJsonValue(top[F("Hours LL")], hours12_Segment.lastLed, 39);
        configComplete &= getJsonValue(top[F("Hours Offset")], hours12_Segment.offset, 0);

        configComplete &= getJsonValue(top[F("H Color")], color, F("0000FF")) && hexStringToColor(color, hourColor, 0x0000FF);
        configComplete &= getJsonValue(top[F("M Unit Color")], color, F("00FF00")) && hexStringToColor(color, minuteUnitColor, 0x00FF00);
        configComplete &= getJsonValue(top[F("M Tens Color")], color, F("00FF00")) && hexStringToColor(color, minuteTensColor, 0x00FF00);
        configComplete &= getJsonValue(top[F("S Unit Color")], color, F("FF0000")) && hexStringToColor(color, secondUnitColor, 0xFF0000);
        configComplete &= getJsonValue(top[F("S Tens Color")], color, F("FF0000")) && hexStringToColor(color, secondTensColor, 0xFF0000);
        configComplete &= getJsonValue(top[F("Moving Effect")], movingEffect, 0);
        configComplete &= getJsonValue(top[F("Marking Mode")], markingMode, 0);
        configComplete &= getJsonValue(top[F("Blend Colors")], blendColors, true);

        if (initDone)
        {
            validateAndUpdate();
        }

        return configComplete;
    }

    void appendConfigData() override
    {
        
        oappend(F("addInfo('")); oappend(FPSTR(_name)); oappend(F(":Overlay Enabled")); oappend(F("',1,'<br><i>(FL: First Led; LL: Last Led)</i>');"));
        oappend(F("addInfo('")); oappend(FPSTR(_name)); oappend(F(":H Color")); oappend(F("',1,'<br><i>(all colors in RRGGBB hex format)</i>');"));

        oappend(F("addCP('")); oappend(FPSTR(_name));oappend(F("','H Color');"));
        oappend(F("addCP('")); oappend(FPSTR(_name));oappend(F("','M Unit Color');"));
        oappend(F("addCP('")); oappend(FPSTR(_name));oappend(F("','M Tens Color');"));
        oappend(F("addCP('")); oappend(FPSTR(_name));oappend(F("','S Unit Color');"));
        oappend(F("addCP('")); oappend(FPSTR(_name));oappend(F("','S Tens Color');"));

        // The following lines add dropdowns for configuring the clock's appearance and behavior.
        oappend(F("dd=addDropdown('"));
        oappend(FPSTR(_name));
        oappend(F("','Moving Effect');"));
        oappend(F("addOption(dd,'Solid',0);"));
        oappend(F("addOption(dd,'Fade',1);"));
        // oappend(F("addInfo('")); oappend(FPSTR(_name)); oappend(F(":Seconds Effect")); oappend(F("',0,'<i>(0:solid; 1:fade)</i>');"));

        oappend(F("dd=addDropdown('"));
        oappend(FPSTR(_name));
        oappend(F("','Marking Mode');"));
        oappend(F("addOption(dd,'Single',0);"));
        oappend(F("addOption(dd,'Cumulative',1);"));
        // oappend(F("addInfo('")); oappend(FPSTR(_name)); oappend(F(":Marking Mode")); oappend(F("',0,'<i>(0:single; 1:cumulative)</i>');"));

        oappend(F("addInfo('"));
        oappend(FPSTR(_name));
        oappend(F(":Blend Colors"));
        oappend(F("',1,'<br><hr style=\"width:50\%\"><b>Usermod v."));
        oappend(getVersion().c_str());
        oappend(F("</b>');"));
    }

    String getVersion()
    {
        return String(FPSTR(_version));
    }
};
// add more strings here to reduce flash memory usage
const char Analog_Segments_ClockUsermod::_name[] PROGMEM = "Analog Segments Clock";
const char Analog_Segments_ClockUsermod::_version[] PROGMEM = "1.0.0";

static Analog_Segments_ClockUsermod analog_segments_clock;
REGISTER_USERMOD(analog_segments_clock);