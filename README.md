# Sonic 3 A.I.R. Plus

Sonic 3 A.I.R. Plus is currently my fork of Sonic 3 A.I.R., letting me experiment on a variety of different things. 

## What this includes

- WIP UWP support 

- WIP Wii U Support

- More render methods for PC

## How can i contribute?

- make a fork and make a pr, if you want to do anything related to Wii U i would hold off on that as my current implementation, while it works, is very messy.

## Wii U notes

- Game ran at 60fps in my testing, with minor dips to 50-40fps. Incredibly playable

- This has been in the works for 4 months. While that could mean that it has SOME level of quality, I am still pretty bad at coding and i wouldn't expect the code to be the best, especially with the variable names


## Frequent issues

"[X] mod doesn't work on Wii U!!" 

It probably does, but to be safe i'd say it doesn't. there are so many things you can do with this engine, i know i didn't get everything.

"I'm seeing a black screen"

Make sure your glslcompiler.rpl is in the right place (though if you unzipped it to the root it should have been done automatically)

otherwise make an issue with your logfile.txt



"Where do i put my ROM? Do i have to rename it?"

Yes, you do have to rename it. But it accepts the normal Sonic 3 A.I.R. ROM naming (Sonic_Knuckles_wSonic3.bin). You just have to put it in savedata instead.

Same goes for mods, put your mods in savedata in a mods folder


"There's nothing showing on my GamePad"

I turned it off that's why (real reason)

i'm serious.

## AI Use notice

While I don't use AI as much as people usually use it for (everything), I will say I did use AI for some things. That being said it keeps removing my comments so idk whats going on there

If you're gonna use ai in a PR, please review the output. I don't wanna have to review your PR just to look at AI slop code

(the thing i mainly used it for was multithreading, cause i got NO clue how to do that on Wii U safely)