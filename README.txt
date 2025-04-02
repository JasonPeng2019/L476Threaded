
i2C takes no pull up or pull down (need to add own pull up / pull down resistor on breadboard)
-> need to add internal pull up or pull down
-> check if internal pull up/pull down resistor
Need to generate new IOC file

enable %f support in STM32CubeIDE

FORMATTING GUIDE:

1) ALL Functions crossed referenced from drivers MUST have driver name in front of them. Example: UART->needs UART in front of function.
Exceptions are middlewares, because middlewares are not high in volume. If middleware count exceeds Base OS usage, start including as well

2) ALL init functions must document descriptions of the structure they are init-including

3) ALL base layer definitions and MACRO definitions must have descriptions for what they are used for, where they are used, etc.

4) ALL driver functions should have USAGE notes detailed at the top.
Clear instructions on how to use the driver should be documented at top.

5) All state machine changes must be done via FUNCTIONS, not DIRECTLY.

6) Multiple param tasks in the scheduler must have a DOUBLE POINTER TYPE param list in the "params" parameter

7) When typecasting data types, you MUST make sure you have filtered it with 
the correct bitwise operations!!! Typecasting between types should be very carefully
done

TODO: 
make guides for UART and console documentation
to make multiple commands for the same module, have to give each instance of module a different ->Description and use
that description in the command name.
to make multiple same commands run, give multiple instances and give different names because
otherwise no way to distinguish
