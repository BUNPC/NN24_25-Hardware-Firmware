-- Firmware for NN22_ControlBoard00
-- Pulse stretcher

-- Initial version: 2023-09-12
-- Bernhard Zimmermann - bzim@bu.edu
-- Boston University Neurophotonics Center

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity pulse_stretcher is
	generic (
		LENGTH : integer := 2**20  -- 2^20/96e6 = 11 ms
	);
	port (
		ClkxCI		: in std_logic;
		ResetxRI 	: in std_logic;
		PulseInxSI	: in std_logic;
		PulseOutxSO	: out std_logic
	);
end pulse_stretcher;

architecture behavioral of pulse_stretcher is

	signal ClkCntxDP, ClkCntxDN : integer range 0 to LENGTH-1;

begin

	p_memzing : process (ClkxCI, ResetxRI)
	begin
		if (ResetxRI = '1') then
			ClkCntxDP <= 0;
		elsif (rising_edge(ClkxCI)) then
			ClkCntxDP <= ClkCntxDN;
		end if;
	end process;

	p_memless : process(ClkCntxDP, PulseInxSI)
	begin
		if PulseInxSI = '1' then
			ClkCntxDN <= LENGTH-1;
		elsif ClkCntxDP = 0 then
			ClkCntxDN <= 0;
		else
			ClkCntxDN <= ClkCntxDP -1;
		end if;
		
		if ClkCntxDP /= 0 then
			PulseOutxSO <= '1';
		else
			PulseOutxSO <= '0';
		end if;

   end process;

end behavioral;